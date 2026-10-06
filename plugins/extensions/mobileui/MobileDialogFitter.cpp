/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "MobileDialogFitter.h"
#include "MobileTheme.h"

#include <KisKineticScroller.h>

#include <kpagemodel.h>
#include <kpageview.h>

#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QDialog>
#include <QDebug>
#include <QScopedValueRollback>
#include <QFontMetrics>
#include <QStyle>
#include <QTreeView>
#include <QRadioButton>
#include <QListView>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHash>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>

namespace mobileui {

namespace {

const char *const SCROLL_NAME = "mobileDialogScroll";

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
    // KoDialog and many Krita forms put their buttons into a box layout of
    // push buttons.
    if (QLayout *l = item->layout()) {
        bool onlyButtons = false;
        for (int i = 0; i < l->count(); ++i) {
            QLayoutItem *child = l->itemAt(i);
            if (child->spacerItem()) {
                continue;
            }
            QWidget *w = child->widget();
            if (!w || !(qobject_cast<QPushButton *>(w) || qobject_cast<QDialogButtonBox *>(w))) {
                return false;
            }
            onlyButtons = true;
        }
        return onlyButtons;
    }
    return false;
}

// A trailing row with the dialog buttons next to other controls (the
// filter dialog: preview, multi-frame, "create filter mask", OK, Cancel).
bool isTrailingControlRow(QLayoutItem *item)
{
    QLayout *l = item->layout();
    if (!l) {
        return false;
    }
    for (int i = 0; i < l->count(); ++i) {
        if (qobject_cast<QDialogButtonBox *>(l->itemAt(i)->widget())) {
            return true;
        }
    }
    return false;
}

// Splits a too wide trailing row into the other controls above and the
// dialog buttons below, so OK and Cancel stay on screen.
QLayout *splitWideRow(QLayout *row, int maxWidth)
{
    QHBoxLayout *h = qobject_cast<QHBoxLayout *>(row);
    if (!h || h->minimumSize().width() <= maxWidth) {
        return row;
    }
    QVBoxLayout *v = new QVBoxLayout;
    v->setContentsMargins(h->contentsMargins());
    QHBoxLayout *controls = new QHBoxLayout;
    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    while (h->count() > 0) {
        QLayoutItem *item = h->takeAt(0);
        QWidget *w = item->widget();
        if (w && (qobject_cast<QDialogButtonBox *>(w))) {
            delete item;
            buttons->addWidget(w);
        } else if (w) {
            delete item;
            controls->addWidget(w);
        } else if (QLayout *sub = item->layout()) {
            sub->setParent(nullptr);
            controls->addLayout(sub);
        } else {
            delete item; // spacers: the rows get their own stretch
        }
    }
    controls->addStretch(1);
    v->addLayout(controls);
    v->addLayout(buttons);
    delete h;
    return v;
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

// Takes the widget or layout out of a layout item that was removed from its
// layout. Returns the widget (the item is deleted) or keeps the item.
QWidget *unwrapWidget(QLayoutItem *&item)
{
    QWidget *w = item->widget();
    if (w) {
        delete item;
        item = nullptr;
    }
    return w;
}

void moveWidget(QWidget *w, QWidget *parent)
{
    const bool hidden = w->isHidden();
    w->setParent(parent);
    w->setHidden(hidden);
}

QScrollArea *createScroll(QWidget *content)
{
    QScrollArea *scroll = new QScrollArea;
    scroll->setObjectName(QLatin1String(SCROLL_NAME));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    KisKineticScroller::createPreconfiguredScroller(scroll);
    return scroll;
}

// The scroll area is a new child of the host and would be destroyed after
// the host's older children, for example the popup frames of Krita's popup
// buttons. Some Krita widgets rely on being destroyed before those (the
// gradient chooser saves its popup's settings in its destructor), so the
// scroll area takes the place of the moved widgets at the front of the
// child list. lower() does exactly that.
void keepDestructionOrder(QWidget *scroll)
{
    scroll->lower();
}

// Widgets like KoVBox add every new child to their layout themselves, so the
// scroll area can end up in the layout twice (once by the widget, once by
// us). Keep the entry with the larger stretch.
void removeDuplicateItems(QBoxLayout *layout)
{
    QHash<QWidget *, int> seen;
    for (int i = 0; i < layout->count(); ++i) {
        QWidget *w = layout->itemAt(i)->widget();
        if (!w) {
            continue;
        }
        if (!seen.contains(w)) {
            seen.insert(w, i);
            continue;
        }
        const int first = seen.value(w);
        const int drop = layout->stretch(first) >= layout->stretch(i) ? i : first;
        delete layout->takeAt(drop);
        if (drop == first) {
            seen.insert(w, i - 1);
        }
        --i;
    }
}

// ---- box layouts --------------------------------------------------------

bool wrapBox(QWidget *host, QBoxLayout *outer, bool keepButtons)
{
    QWidget *content = new QWidget;
    content->setObjectName(QStringLiteral("mobileDialogContent"));
    QBoxLayout *inner = new QBoxLayout(outer->direction(), content);
    inner->setContentsMargins(outer->contentsMargins());
    inner->setSpacing(outer->spacing());

    struct Entry {
        QLayoutItem *item;
        int stretch;
    };
    QList<Entry> move;
    while (outer->count() > 0) {
        const int stretch = outer->stretch(0);
        move.append({outer->takeAt(0), stretch});
    }
    // The trailing button row stays visible below the scroll area.
    QList<Entry> keep;
    const bool vertical = outer->direction() == QBoxLayout::TopToBottom;
    while (keepButtons && vertical && !move.isEmpty()
           && (isButtonRow(move.last().item) || isTrailingControlRow(move.last().item) || move.last().item->spacerItem())) {
        keep.prepend(move.takeLast());
    }
    for (Entry e : move) {
        if (QWidget *w = unwrapWidget(e.item)) {
            moveWidget(w, content);
            inner->addWidget(w, e.stretch);
        } else if (QLayout *l = e.item->layout()) {
            l->setParent(nullptr);
            inner->addLayout(l, e.stretch);
            reparentLayoutWidgets(l, content);
        } else {
            inner->addItem(e.item);
            inner->setStretch(inner->count() - 1, e.stretch);
        }
    }

    outer->setContentsMargins(0, 0, 0, 0);
    QScrollArea *scroll = createScroll(content);
    outer->addWidget(scroll, 1);
    keepDestructionOrder(scroll);
    removeDuplicateItems(outer);
    for (Entry e : keep) {
        if (QWidget *w = unwrapWidget(e.item)) {
            outer->addWidget(w);
        } else if (QLayout *l = e.item->layout()) {
            l->setParent(nullptr);
            l = splitWideRow(l, availableGeometry(host).width());
            outer->addLayout(l);
            reparentLayoutWidgets(l, host);
        } else {
            outer->addItem(e.item);
        }
    }
    return true;
}

// ---- grid layouts -------------------------------------------------------

bool wrapGrid(QWidget *host, QGridLayout *outer, bool keepButtons)
{
    QWidget *content = new QWidget;
    content->setObjectName(QStringLiteral("mobileDialogContent"));
    QGridLayout *inner = new QGridLayout(content);
    inner->setContentsMargins(outer->contentsMargins());
    inner->setHorizontalSpacing(outer->horizontalSpacing());
    inner->setVerticalSpacing(outer->verticalSpacing());
    const int rows = outer->rowCount();
    const int columns = outer->columnCount();
    for (int r = 0; r < rows; ++r) {
        inner->setRowStretch(r, outer->rowStretch(r));
        inner->setRowMinimumHeight(r, outer->rowMinimumHeight(r));
    }
    for (int c = 0; c < columns; ++c) {
        inner->setColumnStretch(c, outer->columnStretch(c));
        inner->setColumnMinimumWidth(c, outer->columnMinimumWidth(c));
    }

    struct Entry {
        QLayoutItem *item;
        int row, column, rowSpan, columnSpan;
    };
    QList<Entry> entries;
    int lastRow = -1;
    for (int i = outer->count() - 1; i >= 0; --i) {
        Entry e;
        outer->getItemPosition(i, &e.row, &e.column, &e.rowSpan, &e.columnSpan);
        e.item = outer->takeAt(i);
        entries.prepend(e);
        lastRow = qMax(lastRow, e.row);
    }
    // A last row with only buttons stays visible below the scroll area.
    bool lastRowIsButtons = keepButtons && lastRow > 0;
    for (const Entry &e : entries) {
        if (e.row + e.rowSpan - 1 == lastRow && !(isButtonRow(e.item) || e.item->spacerItem())) {
            lastRowIsButtons = false;
        }
    }

    for (int r = 0; r < rows; ++r) {
        outer->setRowStretch(r, 0);
        outer->setRowMinimumHeight(r, 0);
    }
    for (int c = 0; c < columns; ++c) {
        outer->setColumnStretch(c, 0);
        outer->setColumnMinimumWidth(c, 0);
    }
    outer->setContentsMargins(0, 0, 0, 0);
    QScrollArea *scroll = createScroll(content);
    outer->addWidget(scroll, 0, 0, 1, qMax(1, columns));
    keepDestructionOrder(scroll);
    outer->setRowStretch(0, 1);

    for (Entry e : entries) {
        const bool keep = lastRowIsButtons && e.row == lastRow;
        QWidget *parent = keep ? host : content;
        QGridLayout *target = keep ? outer : inner;
        const int row = keep ? 1 : e.row;
        const Qt::Alignment alignment = e.item->alignment();
        if (QWidget *w = unwrapWidget(e.item)) {
            moveWidget(w, parent);
            target->addWidget(w, row, e.column, e.rowSpan, e.columnSpan, alignment);
        } else if (QLayout *l = e.item->layout()) {
            l->setParent(nullptr);
            target->addLayout(l, row, e.column, e.rowSpan, e.columnSpan, alignment);
            reparentLayoutWidgets(l, parent);
        } else {
            target->addItem(e.item, row, e.column, e.rowSpan, e.columnSpan, alignment);
        }
    }
    return true;
}

// ---- form layouts -------------------------------------------------------

bool wrapForm(QWidget *host, QFormLayout *outer)
{
    Q_UNUSED(host);
    QWidget *content = new QWidget;
    content->setObjectName(QStringLiteral("mobileDialogContent"));
    QFormLayout *inner = new QFormLayout(content);
    inner->setContentsMargins(outer->contentsMargins());
    inner->setHorizontalSpacing(outer->horizontalSpacing());
    inner->setVerticalSpacing(outer->verticalSpacing());
    inner->setLabelAlignment(outer->labelAlignment());
    inner->setFormAlignment(outer->formAlignment());
    inner->setFieldGrowthPolicy(outer->fieldGrowthPolicy());
    // Phones are narrow: long rows wrap the field below its label.
    inner->setRowWrapPolicy(QFormLayout::WrapLongRows);

    int row = 0;
    while (outer->rowCount() > 0) {
        const QFormLayout::TakeRowResult taken = outer->takeRow(0);
        const QPair<QLayoutItem *, QFormLayout::ItemRole> parts[] = {
            {taken.labelItem, QFormLayout::LabelRole},
            {taken.fieldItem, QFormLayout::FieldRole},
        };
        for (auto part : parts) {
            QLayoutItem *item = part.first;
            if (!item) {
                continue;
            }
            QFormLayout::ItemRole role = part.second;
            if (taken.labelItem && !taken.fieldItem) {
                role = QFormLayout::SpanningRole;
            }
            if (QWidget *w = unwrapWidget(item)) {
                moveWidget(w, content);
                inner->setWidget(row, role, w);
            } else if (QLayout *l = item->layout()) {
                l->setParent(nullptr);
                inner->setLayout(row, role, l);
                reparentLayoutWidgets(l, content);
            } else {
                inner->setItem(row, role, item);
            }
        }
        ++row;
    }
    outer->setContentsMargins(0, 0, 0, 0);
    QScrollArea *scroll = createScroll(content);
    outer->addRow(scroll);
    keepDestructionOrder(scroll);
    return true;
}

// Every top-level page widget of a page view model, including nested pages.
void collectPages(const QAbstractItemModel *model, const QModelIndex &parent, QList<QWidget *> &pages)
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        if (QWidget *page = qvariant_cast<QWidget *>(model->data(index, KPageModel::WidgetRole))) {
            pages.append(page);
        }
        collectPages(model, index, pages);
    }
}

} // namespace

DialogFitter::DialogFitter(QObject *parent)
    : QObject(parent)
{
}

namespace {
const char *const PHONE_ROOT = "mobilePhoneRoot";
const char *const ADAPTED = "mobileAdapted";

bool insidePhoneRoot(const QWidget *widget)
{
    for (const QWidget *w = widget; w; w = w->parentWidget()) {
        if (w->property(PHONE_ROOT).toBool()) {
            return true;
        }
    }
    return false;
}

bool isAdaptable(const QObject *o)
{
    return qobject_cast<const QCheckBox *>(o) || qobject_cast<const QRadioButton *>(o) || qobject_cast<const QLabel *>(o)
        || qobject_cast<const QComboBox *>(o) || qobject_cast<const QListView *>(o) || qobject_cast<const QTreeView *>(o);
}
} // namespace

void DialogFitter::markPhoneRoot(QWidget *root)
{
    if (root) {
        root->setProperty(PHONE_ROOT, true);
    }
}

void DialogFitter::adaptControls(QWidget *root)
{
    if (!root) {
        return;
    }
    markPhoneRoot(root);
    for (QWidget *w : root->findChildren<QWidget *>()) {
        if (isAdaptable(w)) {
            adaptControl(w);
        }
    }
}

void DialogFitter::adaptControl(QWidget *w)
{
    if (w->property(ADAPTED).toBool()) {
        return;
    }
    // Krita's own sheet chrome and the phone interface's widgets are left alone.
    if (w->property("mobileChrome").toBool()) {
        return;
    }
    w->setProperty(ADAPTED, true);
    if (QAbstractButton *button = qobject_cast<QAbstractButton *>(w)) {
        if (button->text().size() < 16) {
            return;
        }
        button->setProperty("mobileOrigText", button->text());
        button->setProperty("mobileOrigMinWidth", button->minimumWidth());
        button->setMinimumWidth(qMin(button->minimumSizeHint().width(), dp(96)));
        button->installEventFilter(this);
        rewrapButton(button);
    } else if (QLabel *label = qobject_cast<QLabel *>(w)) {
        if (label->wordWrap() || label->text().size() < 24 || label->text().contains(QLatin1Char('\n'))) {
            return;
        }
        label->setProperty("mobileOrigWordWrap", false);
        label->setWordWrap(true);
    } else if (QComboBox *combo = qobject_cast<QComboBox *>(w)) {
        combo->setProperty("mobileOrigSizeAdjust", int(combo->sizeAdjustPolicy()));
        combo->setProperty("mobileOrigMinContents", combo->minimumContentsLength());
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(8);
    } else if (QListView *list = qobject_cast<QListView *>(w)) {
        list->setProperty("mobileOrigWordWrap", list->wordWrap());
        list->setProperty("mobileOrigElide", int(list->textElideMode()));
        list->setWordWrap(true);
        list->setTextElideMode(Qt::ElideMiddle);
    } else if (QTreeView *tree = qobject_cast<QTreeView *>(w)) {
        tree->setProperty("mobileOrigElide", int(tree->textElideMode()));
        tree->setTextElideMode(Qt::ElideMiddle);
    }
}

void DialogFitter::rewrapButton(QWidget *w)
{
    QAbstractButton *button = qobject_cast<QAbstractButton *>(w);
    if (!button || m_rewrapping) {
        return;
    }
    const QString original = button->property("mobileOrigText").toString();
    if (original.isEmpty()) {
        return;
    }
    QStyle *style = button->style();
    const bool radio = qobject_cast<QRadioButton *>(button) != nullptr;
    const int indicator = style->pixelMetric(radio ? QStyle::PM_ExclusiveIndicatorWidth : QStyle::PM_IndicatorWidth, nullptr, button)
        + style->pixelMetric(radio ? QStyle::PM_RadioButtonLabelSpacing : QStyle::PM_CheckBoxLabelSpacing, nullptr, button);
    // The space the text gets: the button's own width, or what its parent
    // can offer when the layout hasn't narrowed it yet.
    int width = button->width();
    if (QWidget *parent = button->parentWidget()) {
        width = qMin(width, parent->width() - button->x());
    }
    const int available = width - indicator - dp(8);
    if (available < dp(48)) {
        return;
    }
    QString plain = original;
    plain.remove(QLatin1Char('&'));
    QFontMetrics fm(button->font());
    QString result;
    if (fm.horizontalAdvance(plain) <= available) {
        result = original;
    } else {
        // Greedy word wrap on the original text (mnemonics kept).
        const QStringList words = original.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QString line;
        for (const QString &word : words) {
            const QString candidate = line.isEmpty() ? word : line + QLatin1Char(' ') + word;
            QString measured = candidate;
            measured.remove(QLatin1Char('&'));
            if (!line.isEmpty() && fm.horizontalAdvance(measured) > available) {
                result += (result.isEmpty() ? QString() : QStringLiteral("\n")) + line;
                line = word;
            } else {
                line = candidate;
            }
        }
        if (!line.isEmpty()) {
            result += (result.isEmpty() ? QString() : QStringLiteral("\n")) + line;
        }
    }
    if (result != button->text()) {
        QScopedValueRollback<bool> guard(m_rewrapping, true);
        button->setText(result);
    }
}

void DialogFitter::restoreControls(QWidget *root)
{
    if (!root) {
        return;
    }
    QList<QWidget *> widgets = root->findChildren<QWidget *>();
    widgets.prepend(root);
    for (QWidget *w : widgets) {
        if (!w->property(ADAPTED).toBool()) {
            continue;
        }
        w->setProperty(ADAPTED, QVariant());
        if (QAbstractButton *button = qobject_cast<QAbstractButton *>(w)) {
            if (button->property("mobileOrigText").isValid()) {
                button->removeEventFilter(this);
                button->setText(button->property("mobileOrigText").toString());
                button->setMinimumWidth(button->property("mobileOrigMinWidth").toInt());
                button->setProperty("mobileOrigText", QVariant());
            }
        } else if (QLabel *label = qobject_cast<QLabel *>(w)) {
            if (label->property("mobileOrigWordWrap").isValid()) {
                label->setWordWrap(label->property("mobileOrigWordWrap").toBool());
            }
        } else if (QComboBox *combo = qobject_cast<QComboBox *>(w)) {
            combo->setSizeAdjustPolicy(QComboBox::SizeAdjustPolicy(combo->property("mobileOrigSizeAdjust").toInt()));
            combo->setMinimumContentsLength(combo->property("mobileOrigMinContents").toInt());
        } else if (QAbstractItemView *view = qobject_cast<QAbstractItemView *>(w)) {
            if (QListView *list = qobject_cast<QListView *>(view)) {
                list->setWordWrap(list->property("mobileOrigWordWrap").toBool());
            }
            view->setTextElideMode(Qt::TextElideMode(view->property("mobileOrigElide").toInt()));
        }
    }
    root->setProperty(PHONE_ROOT, QVariant());
}

bool DialogFitter::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize && watched->property("mobileOrigText").isValid()) {
        rewrapButton(static_cast<QWidget *>(watched));
        return QObject::eventFilter(watched, event);
    }
    if (event->type() == QEvent::Show && watched->isWidgetType() && isAdaptable(watched)
        && !watched->property(ADAPTED).toBool() && insidePhoneRoot(static_cast<QWidget *>(watched))) {
        adaptControl(static_cast<QWidget *>(watched));
    }
    if (event->type() == QEvent::Show && watched->isWidgetType()) {
        QDialog *dialog = qobject_cast<QDialog *>(watched);
        // Message boxes and native file dialogs size themselves well.
        if (dialog && dialog->isWindow() && !qobject_cast<QMessageBox *>(dialog) && !qobject_cast<QFileDialog *>(dialog)) {
            fit(dialog);
            // Some dialogs restore their saved desktop size after being
            // shown; fit them again once that happened.
            QPointer<QDialog> guard(dialog);
            QTimer::singleShot(0, this, [this, guard] {
                if (guard && guard->isVisible()) {
                    fit(guard);
                }
            });
        } else if (!dialog && watched->inherits("KisPaintOpSettingsWidget")) {
            stackSideLists(static_cast<QWidget *>(watched));
        }
    }
    return QObject::eventFilter(watched, event);
}

bool DialogFitter::wrapContents(QWidget *host, bool keepButtons)
{
    QLayout *layout = host ? host->layout() : nullptr;
    if (!layout || host->findChild<QScrollArea *>(QLatin1String(SCROLL_NAME), Qt::FindDirectChildrenOnly)) {
        return false;
    }
    if (QBoxLayout *box = qobject_cast<QBoxLayout *>(layout)) {
        return wrapBox(host, box, keepButtons);
    }
    if (QGridLayout *grid = qobject_cast<QGridLayout *>(layout)) {
        return wrapGrid(host, grid, keepButtons);
    }
    if (QFormLayout *form = qobject_cast<QFormLayout *>(layout)) {
        return wrapForm(host, form);
    }
    return false;
}

bool DialogFitter::adaptPageDialog(QDialog *dialog, const QRect &screen)
{
    QWidget *pageView = nullptr;
    for (QWidget *w : dialog->findChildren<QWidget *>()) {
        if (w->inherits("KPageView")) {
            pageView = w;
            break;
        }
    }
    if (!pageView) {
        return false;
    }
    // A page list beside the pages leaves no room on a phone; tabs above
    // the pages keep every page one tap away.
    if (screen.width() < dp(600) || screen.height() < dp(600)) {
        pageView->setProperty("faceType", int(KPageView::Tabbed));
    }
    QAbstractItemView *view = pageView->findChild<QAbstractItemView *>(QString(), Qt::FindDirectChildrenOnly);
    if (!view || !view->model()) {
        return true;
    }
    QList<QWidget *> pages;
    collectPages(view->model(), QModelIndex(), pages);
    // Every page scrolls: the dialog itself is fitted to the screen and
    // pages are not wrapped twice.
    for (QWidget *page : pages) {
        if (!wrapContents(page, false)) {
            qDebug() << "Krita Mobile: page without a supported layout:" << page->metaObject()->className();
        }
    }
    return true;
}

bool DialogFitter::adaptOpenPane(QDialog *dialog, const QRect &screen)
{
    // The new-document dialog has its section list beside the pages; on a
    // narrow screen the list goes above them.
    if (!dialog->inherits("KisOpenPane") || screen.width() >= dp(600)) {
        return false;
    }
    QGridLayout *grid = qobject_cast<QGridLayout *>(dialog->layout());
    QStackedWidget *stack = dialog->findChild<QStackedWidget *>(QStringLiteral("m_widgetStack"));
    QWidget *list = dialog->findChild<QWidget *>(QStringLiteral("m_sectionList"));
    if (!grid || !stack || !list || grid->rowCount() > 1) {
        return false;
    }
    QList<QLayoutItem *> items;
    while (grid->count() > 0) {
        items.append(grid->takeAt(0));
    }
    for (QLayoutItem *item : items) {
        if (item->widget() == stack) {
            delete item;
            grid->addWidget(stack, 1, 0);
        } else if (QLayout *l = item->layout()) {
            l->setParent(nullptr);
            grid->addLayout(l, 0, 0);
        } else if (QWidget *w = unwrapWidget(item)) {
            grid->addWidget(w, 2, 0);
        } else {
            delete item;
        }
    }
    for (int c = 0; c < grid->columnCount(); ++c) {
        grid->setColumnStretch(c, 0);
    }
    grid->setRowStretch(0, 0);
    grid->setRowStretch(1, 1);
    list->setMinimumWidth(0);
    list->setMaximumHeight(dp(150));
    for (int i = 0; i < stack->count(); ++i) {
        wrapContents(stack->widget(i), true);
    }
    // Pages added later (templates) are wrapped when they are shown.
    connect(stack, &QStackedWidget::currentChanged, this, [this, stack](int index) {
        if (QWidget *page = stack->widget(index)) {
            wrapContents(page, true);
        }
    });
    return true;
}

namespace {

void collectItemViews(QLayoutItem *item, QList<QAbstractItemView *> &views, int depth = 0)
{
    if (QAbstractItemView *view = qobject_cast<QAbstractItemView *>(item->widget())) {
        views.append(view);
    } else if (QLayout *l = item->layout()) {
        if (depth < 2) {
            for (int i = 0; i < l->count(); ++i) {
                collectItemViews(l->itemAt(i), views, depth + 1);
            }
        }
    }
}

} // namespace

void DialogFitter::stackSideLists(QWidget *root)
{
    // A list of sections beside a stack of pages (brush settings, layer
    // styles and similar) leaves both unreadable on a phone; put the list
    // above the pages instead. Every brush engine creates its own settings
    // widget, so this also runs whenever one is shown.
    if (!root || availableGeometry(root).width() >= dp(600)) {
        return;
    }
    QList<QBoxLayout *> layouts = root->findChildren<QBoxLayout *>();
    if (QBoxLayout *own = qobject_cast<QBoxLayout *>(root->layout())) {
        if (!layouts.contains(own)) {
            layouts.prepend(own);
        }
    }
    for (QBoxLayout *box : layouts) {
        if (box->direction() != QBoxLayout::LeftToRight) {
            continue;
        }
        bool hasStack = false;
        QList<QAbstractItemView *> views;
        for (int i = 0; i < box->count(); ++i) {
            QLayoutItem *item = box->itemAt(i);
            if (qobject_cast<QStackedWidget *>(item->widget())) {
                hasStack = true;
            } else {
                collectItemViews(item, views);
            }
        }
        if (!hasStack || views.isEmpty()) {
            continue;
        }
        StackedLayout stacked;
        stacked.layout = box;
        for (QAbstractItemView *view : views) {
            stacked.views.append({view, view->minimumWidth(), view->minimumHeight(), view->maximumHeight()});
            view->setMinimumWidth(0);
            view->setMinimumHeight(dp(150));
            view->setMaximumHeight(dp(200));
        }
        m_stackedLayouts.append(stacked);
        box->setDirection(QBoxLayout::TopToBottom);
    }
}

void DialogFitter::restoreWidgets()
{
    for (const StackedLayout &stacked : qAsConst(m_stackedLayouts)) {
        if (stacked.layout) {
            stacked.layout->setDirection(QBoxLayout::LeftToRight);
        }
        for (const StackedView &v : stacked.views) {
            if (v.view) {
                v.view->setMinimumWidth(v.minimumWidth);
                v.view->setMinimumHeight(v.minimumHeight);
                v.view->setMaximumHeight(v.maximumHeight);
            }
        }
    }
    m_stackedLayouts.clear();
}

void DialogFitter::fit(QDialog *dialog)
{
    const QRect screen = availableGeometry(dialog);
    if (!m_adapted.contains(dialog)) {
        m_adapted.insert(dialog);
        // The phone look; appended so a dialog's own rules still win.
        dialog->setStyleSheet(dialogStyleSheet() + dialog->styleSheet());
        adaptControls(dialog);
        connect(dialog, &QObject::destroyed, this, [this, dialog] {
            m_adapted.remove(dialog);
            m_wrapped.remove(dialog);
            m_fullScreen.remove(dialog);
        });
        if (adaptPageDialog(dialog, screen) || adaptOpenPane(dialog, screen)) {
            // Their pages scroll on their own; the dialog only takes the
            // whole screen.
            m_fullScreen.insert(dialog);
        } else {
            stackSideLists(dialog);
        }
    }
    const QSize needed = dialog->minimumSizeHint().expandedTo(dialog->minimumSize());
    if (!m_wrapped.contains(dialog) && !m_fullScreen.contains(dialog)
        && (needed.width() > screen.width() || needed.height() > screen.height())) {
        if (wrapContents(dialog, true)) {
            m_wrapped.insert(dialog);
        }
    }
    // Phones: dialogs that are close to the screen size use all of it, the
    // rest are kept on screen and centered.
    QSize size = dialog->size().expandedTo(dialog->minimumSizeHint());
    if (m_wrapped.contains(dialog) || m_fullScreen.contains(dialog) || size.width() > screen.width() * 0.85 || size.height() > screen.height() * 0.85) {
        dialog->setMinimumSize(0, 0);
        dialog->setGeometry(screen);
    } else {
        size = size.boundedTo(screen.size());
        dialog->resize(size);
        dialog->move(screen.x() + (screen.width() - size.width()) / 2, screen.y() + (screen.height() - size.height()) / 2);
    }
}

} // namespace mobileui
