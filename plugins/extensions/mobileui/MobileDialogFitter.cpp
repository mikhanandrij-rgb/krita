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
#include <QLineEdit>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>

#include <algorithm>

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
    if (!top || top == widget || !top->isVisible() || !top->inherits("KisMainWindow")) {
        // Dialogs without a parent (Layer Style) or shown while another
        // window is active still belong to Krita's main window.
        for (QWidget *w : QApplication::topLevelWidgets()) {
            if (w != widget && w->isVisible() && w->inherits("KisMainWindow")) {
                top = w;
                break;
            }
        }
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

// ---- reflow ---------------------------------------------------------------
// Desktop dialogs put controls side by side; on a phone that makes them wider
// than the screen (and every scroll area scrolls sideways). Rows whose items
// each fit the screen but not side by side are turned into columns, from the
// innermost layouts outwards, until the whole window fits.

bool layoutContains(QLayout *layout, QWidget *w)
{
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);
        if (item->widget() == w) {
            return true;
        }
        if (item->layout() && layoutContains(item->layout(), w)) {
            return true;
        }
    }
    return false;
}

bool inLayout(QWidget *w)
{
    QWidget *parent = w->parentWidget();
    return parent && parent->layout() && layoutContains(parent->layout(), w);
}

int widestItem(QLayout *layout)
{
    int widest = 0;
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);
        if (item->widget() && item->widget()->isHidden()) {
            continue;
        }
        widest = qMax(widest, item->minimumSize().width());
    }
    return widest;
}

// Puts a taken item back into a grid. A nested layout must go back through
// addLayout(): takeAt() released it from its parent, and addItem() would
// leave it without one (not deleted with the widget, not found by
// findChildren(), so never reflowed again).
void placeInGrid(QGridLayout *grid, QLayoutItem *item, int row, int column, int rowSpan, int columnSpan, Qt::Alignment alignment)
{
    if (QLayout *layout = item->layout()) {
        if (!layout->parent()) {
            grid->addLayout(layout, row, column, rowSpan, columnSpan, alignment);
            return;
        }
    }
    grid->addItem(item, row, column, rowSpan, columnSpan, alignment);
}

} // namespace

// Puts every item of a grid into one column, in reading order. Labels end up
// above their fields, like in phone forms. The old positions are kept so the
// grid can be put back (Krita's dockers outlive the phone interface).
void DialogFitter::gridToColumn(QGridLayout *grid, int transposeWidth)
{
    struct Entry {
        QLayoutItem *item;
        int row, column, rowSpan, columnSpan;
        Qt::Alignment alignment;
    };
    QList<Entry> entries;
    for (int i = grid->count() - 1; i >= 0; --i) {
        Entry e;
        grid->getItemPosition(i, &e.row, &e.column, &e.rowSpan, &e.columnSpan);
        e.item = grid->takeAt(i);
        e.alignment = e.item->alignment();
        entries.prepend(e);
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.row != b.row ? a.row < b.row : a.column < b.column;
    });
    const int columns = grid->columnCount();
    const int rows = grid->rowCount();
    QVector<int> columnStretch, columnMinimum, rowStretch, rowMinimum;
    for (int c = 0; c < columns; ++c) {
        columnStretch << grid->columnStretch(c);
        columnMinimum << grid->columnMinimumWidth(c);
        grid->setColumnStretch(c, 0);
        grid->setColumnMinimumWidth(c, 0);
    }
    for (int r = 0; r < rows; ++r) {
        rowStretch << grid->rowStretch(r);
        rowMinimum << grid->rowMinimumHeight(r);
        grid->setRowStretch(r, 0);
        grid->setRowMinimumHeight(r, 0);
    }
    // A small table of fields (a header row of names, a few rows: Clones
    // Array's columns and rows) reads best turned on its side: the names go
    // down the left, each former row becomes a column. Only when that fits.
    bool transpose = false;
    if (rows >= 2 && rows <= 4 && columns >= 3 && columns > rows && transposeWidth >= 0) {
        QVector<int> rowWidth(rows, 0);
        for (const Entry &e : entries) {
            if (e.row < rows && e.rowSpan == 1 && !e.item->isEmpty()) {
                rowWidth[e.row] = qMax(rowWidth[e.row], e.item->minimumSize().width());
            }
        }
        int width = grid->contentsMargins().left() + grid->contentsMargins().right();
        for (int w : rowWidth) {
            width += w + qMax(0, grid->horizontalSpacing());
        }
        transpose = width <= transposeWidth;
    }
    int row = 0;
    for (const Entry &e : entries) {
        if (transpose) {
            placeInGrid(grid, e.item, e.column, e.row, e.columnSpan, e.rowSpan, e.alignment & ~Qt::AlignHorizontal_Mask);
            continue;
        }
        // Labels were right-aligned next to their fields.
        const Qt::Alignment alignment = e.alignment & ~Qt::AlignHorizontal_Mask;
        if (QLabel *label = qobject_cast<QLabel *>(e.item->widget())) {
            const Qt::Alignment text = label->alignment();
            if (text & (Qt::AlignRight | Qt::AlignHCenter)) {
                QPointer<QLabel> guard(label);
                m_undo.append([guard, text] {
                    if (guard) {
                        guard->setAlignment(text);
                    }
                });
                label->setAlignment((text & ~Qt::AlignHorizontal_Mask) | Qt::AlignLeft);
            }
        }
        placeInGrid(grid, e.item, row++, 0, 1, 1, alignment);
    }
    if (!transpose) {
        grid->setColumnStretch(0, 1);
    }

    QPointer<QGridLayout> guard(grid);
    m_undo.append([guard, entries, columnStretch, columnMinimum, rowStretch, rowMinimum] {
        if (!guard) {
            return;
        }
        QList<QLayoutItem *> current;
        while (guard->count() > 0) {
            current.append(guard->takeAt(0));
        }
        int extraRow = rowStretch.size();
        for (QLayoutItem *item : current) {
            // Only pointers are compared: items Krita removed meanwhile are
            // never touched.
            auto it = std::find_if(entries.begin(), entries.end(), [item](const Entry &e) {
                return e.item == item;
            });
            if (it != entries.end()) {
                placeInGrid(guard, item, it->row, it->column, it->rowSpan, it->columnSpan, it->alignment);
            } else {
                placeInGrid(guard, item, extraRow++, 0, 1, 1, item->alignment());
            }
        }
        guard->setColumnStretch(0, 0);
        for (int c = 0; c < columnStretch.size(); ++c) {
            guard->setColumnStretch(c, columnStretch[c]);
            guard->setColumnMinimumWidth(c, columnMinimum[c]);
        }
        for (int r = 0; r < rowStretch.size(); ++r) {
            guard->setRowStretch(r, rowStretch[r]);
            guard->setRowMinimumHeight(r, rowMinimum[r]);
        }
    });
}

void DialogFitter::setMinimumWidthUndoable(QWidget *w, int minimum, int maximum)
{
    QPointer<QWidget> guard(w);
    const int oldMinimum = w->minimumWidth();
    const int oldMaximum = w->maximumWidth();
    m_undo.append([guard, oldMinimum, oldMaximum] {
        if (guard) {
            guard->setMinimumWidth(oldMinimum);
            guard->setMaximumWidth(oldMaximum);
        }
    });
    w->setMinimumWidth(minimum);
    w->setMaximumWidth(maximum);
}

namespace {

int horizontalMargins(const QLayout *l)
{
    const QMargins m = l->contentsMargins();
    return m.left() + m.right();
}

// Sum of the margins of the layouts (top layout down to the innermost one)
// that hold `w`; -1 when `w` is not in this layout.
int marginsTo(QLayout *layout, QWidget *w)
{
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);
        if (item->widget() == w) {
            return horizontalMargins(layout);
        }
        if (QLayout *sub = item->layout()) {
            const int inner = marginsTo(sub, w);
            if (inner >= 0) {
                return horizontalMargins(layout) + inner;
            }
        }
    }
    return -1;
}

// How wide a widget can be inside `root` when `root` is `rootWidth` wide:
// the root's width minus the frames, scroll bars and layout margins of
// everything around it.
class WidthBudget
{
public:
    WidthBudget(QWidget *root, int rootWidth)
        : m_root(root)
        , m_rootWidth(rootWidth)
    {
    }

    int forWidget(QWidget *w)
    {
        if (w == m_root || !w->parentWidget()) {
            return m_rootWidth;
        }
        auto it = m_cache.constFind(w);
        if (it != m_cache.constEnd()) {
            return it.value();
        }
        QWidget *p = w->parentWidget();
        int width = forWidget(p) - overhead(p);
        // Contents of a scroll area that is already laid out: its viewport
        // says exactly how much room there is (the model above misses some
        // margins of Krita's own widgets).
        if (QAbstractScrollArea *area = qobject_cast<QAbstractScrollArea *>(p->parentWidget())) {
            if (area->viewport() == p && p->isVisible() && p->width() > dp(100)) {
                width = qMin(width, p->width());
            }
        }
        if (p->layout()) {
            const int margins = marginsTo(p->layout(), w);
            if (margins > 0) {
                width -= margins;
            }
        }
        m_cache.insert(w, width);
        return width;
    }

    // The space a layout gets from its widget (its own margins included).
    int forLayout(QLayout *l)
    {
        QWidget *owner = l->parentWidget();
        if (!owner) {
            return m_rootWidth;
        }
        int width = forWidget(owner) - overhead(owner);
        for (QObject *o = l->parent(); o && qobject_cast<QLayout *>(o); o = o->parent()) {
            width -= horizontalMargins(static_cast<QLayout *>(o));
        }
        return width;
    }

private:
    static int overhead(QWidget *p)
    {
        int extra = 0;
        if (QAbstractScrollArea *area = qobject_cast<QAbstractScrollArea *>(p)) {
            extra += 2 * area->frameWidth();
            if (area->verticalScrollBarPolicy() != Qt::ScrollBarAlwaysOff) {
                extra += area->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, area);
            }
        } else if (QFrame *frame = qobject_cast<QFrame *>(p)) {
            extra += 2 * frame->frameWidth();
        }
        if (qobject_cast<QGroupBox *>(p) || qobject_cast<QTabWidget *>(p)) {
            extra += dp(8);
        }
        const QMargins m = p->contentsMargins();
        return extra + m.left() + m.right();
    }

    QWidget *m_root;
    int m_rootWidth;
    QHash<QWidget *, int> m_cache;
};

} // namespace

// A widget that now sits in a column of its own fills the column, the way
// phone forms look, instead of keeping its fixed desktop width.
void DialogFitter::fillColumn(QWidget *w)
{
    if (!w || w->property("mobileChrome").toBool()) {
        return;
    }
    const QSizePolicy policy = w->sizePolicy();
    const QSizePolicy::Policy h = policy.horizontalPolicy();
    if (h != QSizePolicy::Fixed && h != QSizePolicy::Maximum) {
        return;
    }
    const bool input = qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QComboBox *>(w) || qobject_cast<QLineEdit *>(w)
        || qobject_cast<QAbstractSlider *>(w) || qobject_cast<QGroupBox *>(w) || w->layout();
    if (!input) {
        return;
    }
    QPointer<QWidget> guard(w);
    const int maximum = w->maximumWidth();
    m_undo.append([guard, policy, maximum] {
        if (guard) {
            guard->setSizePolicy(policy);
            guard->setMaximumWidth(maximum);
        }
    });
    QSizePolicy wide = policy;
    wide.setHorizontalPolicy(QSizePolicy::Preferred);
    w->setSizePolicy(wide);
    if (maximum < QWIDGETSIZE_MAX && !w->layout()) {
        w->setMaximumWidth(QWIDGETSIZE_MAX);
    }
}

void DialogFitter::fillColumnItems(QLayout *layout)
{
    if (layout->parentWidget() && layout->parentWidget()->layout() == layout) {
        fillColumn(layout->parentWidget());
    }
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);
        if (QWidget *w = item->widget()) {
            fillColumn(w);
            if (item->alignment() & Qt::AlignHorizontal_Mask) {
                const Qt::Alignment old = item->alignment();
                item->setAlignment(old & ~Qt::AlignHorizontal_Mask);
                QPointer<QLayout> guard(layout);
                m_undo.append([guard, w, old] {
                    // Only if the widget is still in that layout.
                    if (guard && guard->indexOf(w) >= 0) {
                        guard->itemAt(guard->indexOf(w))->setAlignment(old);
                    }
                });
            }
        }
    }
}

int DialogFitter::reflowStructure(QWidget *root, int width, QWidget *budgetRoot)
{
    int changed = 0;
    WidthBudget budget(budgetRoot ? budgetRoot : root, width);
    QList<QLayout *> layouts = root->findChildren<QLayout *>();
    if (root->layout() && !layouts.contains(root->layout())) {
        layouts.prepend(root->layout());
    }
    for (QWidget *w : root->findChildren<QWidget *>()) {
        if (w->isWindow() || w->property("mobileChrome").toBool()) {
            continue;
        }
        const int limit = budget.forWidget(w);
        // Column-shaped panels with a fixed desktop width (canvas size)
        // use the width of the phone.
        if (QLayout *own = w->layout()) {
            QBoxLayout *box = qobject_cast<QBoxLayout *>(own);
            const bool column = (box && box->direction() == QBoxLayout::TopToBottom) || qobject_cast<QFormLayout *>(own)
                || (qobject_cast<QGridLayout *>(own) && static_cast<QGridLayout *>(own)->columnCount() == 1);
            if (column && w->sizePolicy().horizontalPolicy() == QSizePolicy::Fixed && inLayout(w)) {
                fillColumn(w);
                ++changed;
            }
        }
        if (QDialogButtonBox *box = qobject_cast<QDialogButtonBox *>(w)) {
            if (box->orientation() == Qt::Horizontal && box->minimumSizeHint().width() > limit) {
                QPointer<QDialogButtonBox> guard(box);
                m_undo.append([guard] {
                    if (guard) {
                        guard->setOrientation(Qt::Horizontal);
                    }
                });
                box->setOrientation(Qt::Vertical);
                ++changed;
            }
        } else if (QSplitter *splitter = qobject_cast<QSplitter *>(w)) {
            // Panels side by side (brush editor: presets, settings, scratch
            // pad) go below each other as soon as they want more room than
            // the screen has, not only when they can't be squeezed any more.
            if (splitter->orientation() == Qt::Horizontal && splitter->count() > 1
                && splitter->sizeHint().width() > limit) {
                QPointer<QSplitter> guard(splitter);
                const QList<int> sizes = splitter->sizes();
                m_undo.append([guard, sizes] {
                    if (guard) {
                        guard->setOrientation(Qt::Horizontal);
                        guard->setSizes(sizes);
                    }
                });
                splitter->setOrientation(Qt::Vertical);
                // The old widths would become heights; every panel gets the
                // height it asks for instead (the sheet scrolls).
                QList<int> heights;
                for (int i = 0; i < splitter->count(); ++i) {
                    QWidget *panel = splitter->widget(i);
                    heights << (panel->isHidden() ? 0 : qMax(panel->sizeHint().height(), panel->minimumSizeHint().height()));
                }
                splitter->setSizes(heights);
                ++changed;
            }
        } else if (QTabBar *bar = qobject_cast<QTabBar *>(w)) {
            if (!bar->usesScrollButtons() || bar->expanding()) {
                QPointer<QTabBar> guard(bar);
                const bool scroll = bar->usesScrollButtons();
                const bool expanding = bar->expanding();
                m_undo.append([guard, scroll, expanding] {
                    if (guard) {
                        guard->setUsesScrollButtons(scroll);
                        guard->setExpanding(expanding);
                    }
                });
                bar->setUsesScrollButtons(true);
                bar->setExpanding(false);
                bar->updateGeometry();
                if (bar->parentWidget()) {
                    bar->parentWidget()->updateGeometry();
                }
                ++changed;
            }
        }
    }
    // Rows that only overflow because their items are side by side.
    for (QLayout *l : layouts) {
        const int limit = budget.forLayout(l);
        if (l->minimumSize().width() <= limit || widestItem(l) > limit - horizontalMargins(l)) {
            continue;
        }
        if (QBoxLayout *box = qobject_cast<QBoxLayout *>(l)) {
            const QBoxLayout::Direction direction = box->direction();
            if (direction == QBoxLayout::LeftToRight || direction == QBoxLayout::RightToLeft) {
                QPointer<QBoxLayout> guard(box);
                m_undo.append([guard, direction] {
                    if (guard) {
                        guard->setDirection(direction);
                    }
                });
                box->setDirection(QBoxLayout::TopToBottom);
                fillColumnItems(box);
                ++changed;
            }
        } else if (QGridLayout *grid = qobject_cast<QGridLayout *>(l)) {
            if (grid->columnCount() > 1) {
                gridToColumn(grid, limit);
                fillColumnItems(grid);
                ++changed;
            }
        } else if (QFormLayout *form = qobject_cast<QFormLayout *>(l)) {
            const QFormLayout::RowWrapPolicy policy = form->rowWrapPolicy();
            if (policy != QFormLayout::WrapAllRows) {
                QPointer<QFormLayout> guard(form);
                m_undo.append([guard, policy] {
                    if (guard) {
                        guard->setRowWrapPolicy(policy);
                    }
                });
                form->setRowWrapPolicy(QFormLayout::WrapAllRows);
                ++changed;
            }
        }
    }
    return changed;
}

int DialogFitter::reflowControls(QWidget *root, int width, QWidget *budgetRoot)
{
    int changed = 0;
    WidthBudget budget(budgetRoot ? budgetRoot : root, width);
    for (QWidget *w : root->findChildren<QWidget *>()) {
        if (w->isWindow() || !inLayout(w) || w->property("mobileChrome").toBool()) {
            continue;
        }
        if (qobject_cast<QDialogButtonBox *>(w) || qobject_cast<QSplitter *>(w)) {
            continue;
        }
        const int limit = budget.forWidget(w);
        if (limit < dp(48)) {
            continue; // something around it is still too wide
        }
        // A size policy that makes the preferred width the minimum one
        // (Fixed, Minimum) keeps a widget wider than the phone although it
        // could shrink.
        {
            const QSizePolicy policy = w->sizePolicy();
            const QSizePolicy::Policy h = policy.horizontalPolicy();
            const bool hintIsMinimum = h == QSizePolicy::Fixed || h == QSizePolicy::Minimum || h == QSizePolicy::MinimumExpanding;
            if (hintIsMinimum && w->minimumWidth() == 0 && w->sizeHint().width() > limit && w->minimumSizeHint().width() <= limit) {
                QPointer<QWidget> guard(w);
                m_undo.append([guard, policy] {
                    if (guard) {
                        guard->setSizePolicy(policy);
                    }
                });
                QSizePolicy shrinkable = policy;
                shrinkable.setHorizontalPolicy(h == QSizePolicy::MinimumExpanding ? QSizePolicy::Expanding : QSizePolicy::Preferred);
                w->setSizePolicy(shrinkable);
                ++changed;
                continue;
            }
        }
        if (w->layout() || qobject_cast<QTabWidget *>(w) || qobject_cast<QStackedWidget *>(w)
            || qobject_cast<QAbstractScrollArea *>(w)) {
            // Containers: their contents are reflowed. A fixed desktop
            // minimum width is lifted, and a container that is only wider
            // than its contents (a long group box title) gets their width.
            if (w->minimumWidth() > limit) {
                setMinimumWidthUndoable(w, 0, QWIDGETSIZE_MAX);
                ++changed;
            } else if (qobject_cast<QGroupBox *>(w) && w->minimumWidth() == 0 && w->layout() && w->minimumSizeHint().width() > limit) {
                const int contents = w->layout()->minimumSize().width();
                if (contents > 0 && contents <= limit) {
                    setMinimumWidthUndoable(w, contents, w->maximumWidth());
                    ++changed;
                }
            }
            continue;
        }
        const int minimum = w->minimumWidth() > 0 ? w->minimumWidth() : w->minimumSizeHint().width();
        if (minimum <= limit) {
            continue;
        }
        QLabel *label = qobject_cast<QLabel *>(w);
        if (label && !label->wordWrap() && label->text().contains(QLatin1Char(' ')) && w->minimumWidth() <= limit) {
            QPointer<QLabel> guard(label);
            m_undo.append([guard] {
                if (guard) {
                    guard->setWordWrap(false);
                }
            });
            label->setWordWrap(true);
        } else {
            setMinimumWidthUndoable(w, qMin(limit, dp(120)),
                                    w->maximumWidth() < QWIDGETSIZE_MAX ? qMax(dp(120), qMin(w->maximumWidth(), limit)) : QWIDGETSIZE_MAX);
        }
        ++changed;
    }
    return changed;
}

namespace {
// The window fits, and no scroll area inside it scrolls sideways.
bool fitsWidth(QWidget *root, int width)
{
    if (root->minimumSizeHint().width() > width) {
        return false;
    }
    for (QScrollArea *area : root->findChildren<QScrollArea *>()) {
        QWidget *content = area->widget();
        if (area->isVisibleTo(root) && area->widgetResizable() && content
            && content->minimumSizeHint().width() > qMax(area->viewport()->width(), width - dp(16))) {
            return false;
        }
    }
    return true;
}

// The innermost widgets that are still too wide, with the chain of their
// parents, for the log.
void logTooWide(QWidget *root, int width)
{
    int logged = 0;
    for (QWidget *w : root->findChildren<QWidget *>()) {
        if (w->isWindow() || w->isHidden() || logged > 12) {
            continue;
        }
        auto wide = [width](QWidget *x) {
            const int min = x->minimumWidth() > 0 ? x->minimumWidth() : x->minimumSizeHint().width();
            return min > width - dp(16);
        };
        if (!wide(w)) {
            continue;
        }
        bool childWide = false;
        for (QWidget *c : w->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (!c->isWindow() && !c->isHidden() && wide(c)) {
                childWide = true;
                break;
            }
        }
        if (childWide) {
            continue;
        }
        QStringList chain;
        for (QWidget *p = w; p && p != root; p = p->parentWidget()) {
            const QLayout *l = p->layout();
            chain << QStringLiteral("%1'%2'[min %3 hint %4 layout %5 %6]")
                         .arg(QString::fromLatin1(p->metaObject()->className()), p->objectName())
                         .arg(p->minimumWidth())
                         .arg(p->minimumSizeHint().width())
                         .arg(l ? QString::fromLatin1(l->metaObject()->className()) : QStringLiteral("-"))
                         .arg(l ? l->minimumSize().width() : -1);
        }
        qInfo().noquote() << "Krita Mobile: too wide in" << root->metaObject()->className() << chain.join(QStringLiteral(" < "));
        ++logged;
    }
}
} // namespace

int DialogFitter::reflow(QWidget *root, int width, QWidget *budgetRoot)
{
    if (!root || width < dp(200)) {
        return 0;
    }
    int changes = 0;
    // Rows become columns first; only what is still too wide on its own is
    // squeezed. A few pixels are kept for styles that draw outside.
    const int budget = width - dp(4);
    for (int pass = 0; pass < 16; ++pass) {
        if (pass > 0) {
            // Layouts cache the sizes of their items; after a change they
            // must be measured again. (Only then: invalidating posts a
            // layout request, which would start another reflow.)
            // Layout items also cache their widget's sizes until the widget
            // reports a change; containers like tab widgets don't always.
            for (QWidget *w : root->findChildren<QWidget *>()) {
                if (!w->isWindow()) {
                    w->updateGeometry();
                }
            }
            for (QLayout *l : root->findChildren<QLayout *>()) {
                l->invalidate();
            }
            if (root->layout()) {
                root->layout()->invalidate();
            }
        }
        int changed = reflowStructure(root, budget, budgetRoot);
        if (!changed) {
            changed = reflowControls(root, budget, budgetRoot);
        }
        changes += changed;
        if (!changed) {
            break;
        }
    }
    if (changes && root->layout()) {
        root->layout()->activate();
    }
    if (qEnvironmentVariableIsSet("KRITA_MOBILE_DUMP")) {
        qInfo() << "Krita Mobile: reflow" << root->metaObject()->className() << root->objectName() << "width" << width << "changes" << changes;
    }
    // A part of a window is checked against the room it has in it.
    const int own = budgetRoot && budgetRoot != root ? WidthBudget(budgetRoot, width).forWidget(root) : width;
    if (qEnvironmentVariableIsSet("KRITA_MOBILE_DUMP") && !fitsWidth(root, own) && !m_loggedTooWide.contains(root->metaObject()->className())) {
        m_loggedTooWide.insert(root->metaObject()->className());
        logTooWide(root, own);
    }
    return changes;
}

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
        button->setProperty("mobileOrigPolicy", int(button->sizePolicy().horizontalPolicy()));
        button->setMinimumWidth(qMin(button->minimumSizeHint().width(), dp(96)));
        // The wrapped text makes the button's own size hint narrow; it
        // takes the row's width instead, and wraps again when that changes.
        QSizePolicy policy = button->sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Expanding);
        button->setSizePolicy(policy);
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
    // Never narrower than a readable column: a button measured while its
    // layout was still settling would otherwise keep a one-word-per-line
    // text (and a size hint to match) for good.
    const int parentRoom = button->parentWidget() ? button->parentWidget()->width() - indicator - dp(16) : width;
    const int available = qMin(qMax(width - indicator - dp(8), dp(180)), qMax(parentRoom, dp(120)));
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
                QSizePolicy policy = button->sizePolicy();
                policy.setHorizontalPolicy(QSizePolicy::Policy(button->property("mobileOrigPolicy").toInt()));
                button->setSizePolicy(policy);
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
    if (event->type() == QEvent::Resize && watched->isWidgetType()) {
        // A phone panel got a new width (rotation, the window was resized).
        QWidget *w = static_cast<QWidget *>(watched);
        if (!w->isWindow() && w->isVisible() && w->property(PHONE_ROOT).toBool()) {
            QStackedWidget *stack = qobject_cast<QStackedWidget *>(w);
            QWidget *page = stack ? stack->currentWidget() : w;
            if (page) {
                scheduleReflow(page, w);
            }
        }
    }
    if (event->type() == QEvent::LayoutRequest && watched->isWidgetType()) {
        // Sizes change after a window was first shown (the phone style is
        // applied to its controls, combo boxes are filled): reflow again.
        QWidget *w = static_cast<QWidget *>(watched);
        if (w->isVisible()) {
            QDialog *dialog = qobject_cast<QDialog *>(w);
            if (dialog && m_adapted.contains(dialog)) {
                scheduleReflow(dialog, dialog);
            } else if (m_popups.contains(w)) {
                scheduleReflow(w, w);
            } else if (!w->isWindow() && w->property(PHONE_ROOT).toBool()) {
                QStackedWidget *stack = qobject_cast<QStackedWidget *>(w);
                QWidget *page = stack ? stack->currentWidget() : w;
                if (page) {
                    scheduleReflow(page, w);
                }
            }
        }
    }
    if (event->type() == QEvent::Show && watched->isWidgetType()) {
        QWidget *w = static_cast<QWidget *>(watched);
        if (w->isWindow() && w->windowType() == Qt::Popup && !qobject_cast<QMenu *>(w) && !w->inherits("QComboBoxPrivateContainer")
            && !w->inherits("QCompleter") && !w->inherits("QCalendarWidget") && !w->property("mobileChrome").toBool()) {
            fitPopup(w);
        } else if (QMenu *menu = qobject_cast<QMenu *>(w)) {
            styleMenu(menu);
        } else if (!w->isWindow() && (w->layout() || qobject_cast<QAbstractScrollArea *>(w))) {
            // Pages and settings widgets shown later inside the sheet or a
            // fitted dialog (another brush engine, another tab) are reflowed
            // too.
            for (QWidget *p = w; p; p = p->parentWidget()) {
                if (p->property(PHONE_ROOT).toBool() && !qobject_cast<QDialog *>(p)) {
                    scheduleReflow(w, p);
                    break;
                }
                if (p->isWindow()) {
                    if (QDialog *dialog = qobject_cast<QDialog *>(p)) {
                        if (m_adapted.contains(dialog)) {
                            scheduleReflow(w, dialog);
                        }
                    }
                    break;
                }
            }
        }
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

void DialogFitter::scheduleReflow(QWidget *shown, QWidget *root)
{
    // Showing a page shows all its children too; collect them and reflow
    // only the outermost ones once the event loop is back.
    const bool first = m_pendingReflow.isEmpty();
    m_pendingReflow.insert(shown, qMakePair(QPointer<QWidget>(shown), QPointer<QWidget>(root)));
    if (!first) {
        return;
    }
    QTimer::singleShot(0, this, [this] {
        const auto pending = m_pendingReflow;
        m_pendingReflow.clear();
    m_popups.clear();
        for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) {
            QWidget *w = it.value().first;
            QWidget *root = it.value().second;
            if (!w || !root || !w->isVisible()) {
                continue;
            }
            bool nested = false;
            for (QWidget *p = w->parentWidget(); p && !nested; p = p->parentWidget()) {
                nested = pending.contains(p);
                if (p == root) {
                    break;
                }
            }
            if (nested) {
                continue;
            }
            // The sheet may not be laid out yet (a new widget is 640 px wide
            // until then); it is never wider than the window.
            int width = availableGeometry(root).width();
            if (!root->isWindow() && root->isVisible() && root->width() > dp(200)) {
                width = qMin(width, root->width());
            }
            if (QDialog *dialog = qobject_cast<QDialog *>(w)) {
                if (dialog == root && m_adapted.contains(dialog)) {
                    fit(dialog);
                    continue;
                }
            }
            if (w == root && m_popups.contains(w)) {
                fitPopup(w);
                continue;
            }
            if (width > dp(200) && reflow(w, width, root) > 0 && w->isWindow()) {
                // Keep the window inside the screen after it narrowed.
                const QRect screen = availableGeometry(w);
                if (w->width() > screen.width() || w->x() < screen.x()) {
                    QRect g = w->geometry();
                    g.setWidth(qMin(g.width(), screen.width()));
                    g.moveLeft(qMax(screen.x(), qMin(g.x(), screen.right() - g.width() + 1)));
                    w->setGeometry(g);
                }
            }
        }
    });
}

void DialogFitter::adaptPanel(QWidget *root)
{
    if (!root) {
        return;
    }
    adaptControls(root);
    QStackedWidget *stack = qobject_cast<QStackedWidget *>(root);
    QWidget *page = stack && stack->currentWidget() ? stack->currentWidget() : root;
    scheduleReflow(page, root);
}

void DialogFitter::styleMenu(QMenu *menu)
{
    // Context menus (layers, canvas, tool options): finger-sized rows.
    if (menu->property("mobileChrome").toBool()) {
        return;
    }
    if (menu->inherits("KateCommandBar")) {
        // Krita's command search: a search field and a list, sized for a
        // desktop editor (40% of the window). On a phone it gets the
        // window's width below the top bar.
        if (!menu->property("mobileMenu").toBool()) {
            menu->setProperty("mobileMenu", true);
            const QString own = menu->styleSheet();
            QPointer<QMenu> guard(menu);
            m_undo.append([guard, own] {
                if (guard) {
                    guard->setStyleSheet(own);
                    guard->setProperty("mobileMenu", QVariant());
                }
            });
            menu->setStyleSheet(dialogStyleSheet() + menuStyleSheet() + own);
        }
        QPointer<QMenu> guard(menu);
        QTimer::singleShot(0, this, [this, guard] {
            if (!guard || !guard->isVisible()) {
                return;
            }
            const QRect screen = availableGeometry(guard);
            const int width = qMin(screen.width() - dp(16), dp(560));
            if (guard->minimumWidth() != width) {
                const int oldMinimum = guard->minimumWidth();
                m_undo.append([guard, oldMinimum] {
                    if (guard) {
                        guard->setMinimumWidth(oldMinimum);
                    }
                });
                guard->setMinimumWidth(width);
            }
            guard->setGeometry(screen.x() + (screen.width() - width) / 2, screen.y() + dp(56), width, screen.height() / 2);
        });
        return;
    }
    if (menu->property("mobileMenu").toBool()) {
        return;
    }
    menu->setProperty("mobileMenu", true);
    const QString own = menu->styleSheet();
    QPointer<QMenu> guard(menu);
    m_undo.append([guard, own] {
        if (guard) {
            guard->setStyleSheet(own);
            guard->setProperty("mobileMenu", QVariant());
        }
    });
    menu->setStyleSheet(menuStyleSheet() + own);
}

void DialogFitter::fitPopup(QWidget *popup)
{
    const QRect screen = availableGeometry(popup);
    if (!m_popups.contains(popup)) {
        m_popups.insert(popup);
        connect(popup, &QObject::destroyed, this, [this, popup] {
            m_popups.remove(popup);
        });
        // The phone look: rounded surface, finger-sized controls.
        const QString own = popup->styleSheet();
        QPointer<QWidget> guard(popup);
        m_undo.append([guard, own] {
            if (guard) {
                guard->setStyleSheet(own);
            }
        });
        popup->setProperty("mobilePopup", true);
        popup->setStyleSheet(dialogStyleSheet() + popupStyleSheet() + own);
        adaptControls(popup);
    }
    reflow(popup, screen.width() - dp(16));
    // Keep it on screen: Krita places popups next to their buttons, which
    // on a phone may be partly outside.
    QPointer<QWidget> guard(popup);
    QTimer::singleShot(0, this, [guard, screen] {
        if (!guard || !guard->isVisible()) {
            return;
        }
        QRect g = guard->geometry();
        const QSize bounded = g.size().boundedTo(screen.size());
        if (bounded != g.size()) {
            guard->resize(bounded);
            g.setSize(guard->size());
        }
        g.moveRight(qMin(g.right(), screen.right()));
        g.moveBottom(qMin(g.bottom(), screen.bottom()));
        g.moveLeft(qMax(g.left(), screen.left()));
        g.moveTop(qMax(g.top(), screen.top()));
        if (g.topLeft() != guard->pos()) {
            guard->move(g.topLeft());
        }
    });
}

void DialogFitter::restoreWidgets()
{
    while (!m_undo.isEmpty()) {
        m_undo.takeLast()();
    }
    m_pendingReflow.clear();
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
    // Dialogs with a fixed size (color dialogs) could not be fitted at all.
    if (QLayout *layout = dialog->layout()) {
        if (layout->sizeConstraint() == QLayout::SetFixedSize || layout->sizeConstraint() == QLayout::SetMinimumSize) {
            layout->setSizeConstraint(QLayout::SetDefaultConstraint);
            dialog->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        }
    }
    if (dialog->minimumWidth() > screen.width()) {
        dialog->setMinimumWidth(0);
    }
    if (dialog->minimumHeight() > screen.height()) {
        dialog->setMinimumHeight(0);
    }
    // Side by side controls become columns, so nothing scrolls sideways.
    reflow(dialog, screen.width());
    const QSize needed = dialog->minimumSizeHint().expandedTo(dialog->minimumSize());
    if (!m_wrapped.contains(dialog) && !m_fullScreen.contains(dialog)
        && (needed.width() > screen.width() || needed.height() > screen.height())) {
        if (wrapContents(dialog, true)) {
            m_wrapped.insert(dialog);
        }
    }
    // Phones: dialogs that are close to the screen size use all of it, the
    // rest are kept on screen and centered.
    // Small dialogs are cards: the phone's width (with a margin), their own
    // height. Tall ones and page dialogs take the whole screen.
    const QSize hint = dialog->sizeHint().expandedTo(dialog->minimumSizeHint());
    // Upright phone: every card has the same width.
    const int cardWidth = screen.width() < dp(600) ? screen.width() - dp(32) : qMin(screen.width() - dp(32), qMax(hint.width(), dp(360)));
    const bool card = !m_wrapped.contains(dialog) && !m_fullScreen.contains(dialog)
        && dialog->minimumSizeHint().width() <= cardWidth && hint.height() <= screen.height() * 0.85;
    if (!card) {
        dialog->setMinimumSize(0, 0);
        dialog->setGeometry(screen);
    } else {
        QSize size(cardWidth, qMin(hint.height(), screen.height()));
        dialog->resize(size);
        dialog->move(screen.x() + (screen.width() - size.width()) / 2, screen.y() + (screen.height() - size.height()) / 2);
    }
}

} // namespace mobileui
