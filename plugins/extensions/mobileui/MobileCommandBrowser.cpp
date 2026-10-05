/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Ported from the Drawpile Mobile fork (same author, GPL-3.0-or-later) and
 * extended with Krita's action categories.
 */
#include "MobileCommandBrowser.h"
#include "MobileTheme.h"
#include "MobileWidgets.h"
#include <klocalizedstring.h>

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QSet>

#include <kis_action_registry.h>

#include <algorithm>

namespace mobileui {

namespace {

QString menuIconFor(QMenu *menu)
{
    // Krita's xmlgui menu names (krita5.xmlgui).
    static const QPair<const char *, const char *> icons[] = {
        {"file", "file"},     {"edit", "undo"},       {"view", "eye"},
        {"image", "resize"},  {"layer", "layers"},    {"select", "square"},
        {"filter", "sliders"}, {"tools", "tools"},    {"settings", "settings"},
        {"window", "more"},   {"help", "info"},
    };
    const QString name = menu->objectName();
    for (const auto &pair : icons) {
        if (name == QLatin1String(pair.first)) {
            return QString::fromLatin1(pair.second);
        }
    }
    return QStringLiteral("info");
}

// One row in the command list.
class CommandRow final : public QAbstractButton {
public:
    enum class Trailing { None, Chevron, Check, Radio, Switch };

    CommandRow(
        const QIcon &rowIcon, const QString &text, const QString &subtitle,
        Trailing trailing, bool on, QWidget *parent = nullptr)
        : QAbstractButton(parent)
        , m_icon(rowIcon)
        , m_subtitle(subtitle)
        , m_trailing(trailing)
        , m_on(on)
    {
        setText(text);
        setAccessibleName(text);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        return QSize(dp(200), m_subtitle.isEmpty() ? dp(52) : dp(62));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Theme &t = Theme::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if(isDown()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(t.surface3);
            painter.drawRoundedRect(
                QRectF(rect()).adjusted(dp(8), dp(2), -dp(8), -dp(2)),
                radiusSmall(), radiusSmall());
        }

        int x = dp(20);
        int is = dp(22);
        QIcon::Mode mode = isEnabled() ? QIcon::Normal : QIcon::Disabled;
        if(!m_icon.isNull()) {
            m_icon.paint(
                &painter, QRect(x, (height() - is) / 2, is, is),
                Qt::AlignCenter, mode);
        }
        x += is + dp(16);

        int trailingWidth = m_trailing == Trailing::None ? 0 : dp(52);
        QRectF textRect(x, 0, width() - x - trailingWidth - dp(8), height());
        QFont titleFont = font();
        titleFont.setPixelSize(fontPixelSize(TextRole::Body));
        QFontMetrics tfm(titleFont);
        painter.setFont(titleFont);
        painter.setPen(isEnabled() ? t.text : t.textDim);
        if(m_subtitle.isEmpty()) {
            painter.drawText(
                textRect, Qt::AlignVCenter | Qt::AlignLeft,
                tfm.elidedText(text(), Qt::ElideRight, int(textRect.width())));
        } else {
            QFont subFont = font();
            subFont.setPixelSize(fontPixelSize(TextRole::Caption));
            QFontMetrics sfm(subFont);
            qreal total = tfm.height() + sfm.height();
            qreal y = (height() - total) / 2.0;
            painter.drawText(
                QRectF(x, y, textRect.width(), tfm.height()),
                Qt::AlignLeft | Qt::AlignVCenter,
                tfm.elidedText(text(), Qt::ElideRight, int(textRect.width())));
            painter.setFont(subFont);
            painter.setPen(t.textDim);
            painter.drawText(
                QRectF(x, y + tfm.height(), textRect.width(), sfm.height()),
                Qt::AlignLeft | Qt::AlignVCenter,
                sfm.elidedText(
                    m_subtitle, Qt::ElideRight, int(textRect.width())));
        }

        QPointF c(width() - dp(36), height() / 2.0);
        QColor fg = isEnabled() ? t.text : t.textDim;
        switch(m_trailing) {
        case Trailing::None:
            break;
        case Trailing::Chevron:
            mobileui::icon(QStringLiteral("chevron-right"), t.textDim)
                .paint(
                    &painter,
                    QRect(int(c.x() - dp(10)), int(c.y() - dp(10)), dp(20),
                          dp(20)),
                    Qt::AlignCenter, mode);
            break;
        case Trailing::Check:
            if(m_on) {
                mobileui::icon(QStringLiteral("check"), t.accent)
                    .paint(
                        &painter,
                        QRect(int(c.x() - dp(11)), int(c.y() - dp(11)),
                              dp(22), dp(22)),
                        Qt::AlignCenter, mode);
            }
            break;
        case Trailing::Radio: {
            qreal r = dp(10);
            painter.setPen(QPen(m_on ? t.accent : fg, dp(2)));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(c, r, r);
            if(m_on) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(t.accent);
                painter.drawEllipse(c, r * 0.5, r * 0.5);
            }
            break;
        }
        case Trailing::Switch: {
            qreal w = dp(40), h = dp(24);
            QRectF track(c.x() - w / 2.0, c.y() - h / 2.0, w, h);
            painter.setPen(Qt::NoPen);
            QColor trackColor = m_on ? t.accent : t.surface3;
            if(!isEnabled()) {
                trackColor.setAlphaF(0.4);
            }
            painter.setBrush(trackColor);
            painter.drawRoundedRect(track, h / 2.0, h / 2.0);
            qreal knob = h - dp(6);
            qreal kx = m_on ? track.right() - dp(3) - knob : track.left() + dp(3);
            painter.setBrush(m_on ? t.accentText : t.textDim);
            painter.drawEllipse(
                QRectF(kx, track.top() + dp(3), knob, knob));
            break;
        }
        }
    }

private:
    QIcon m_icon;
    QString m_subtitle;
    Trailing m_trailing;
    bool m_on;
};

QFrame *makeDivider()
{
    QFrame *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: %1; margin: 0px %2px;")
                            .arg(Theme::current().outline.name())
                            .arg(dp(20)));
    return line;
}

}

CommandBrowser::CommandBrowser(QMenuBar *menuBar, QWidget *parent)
    : QWidget(parent)
    , m_menuBar(menuBar)
{
    setProperty("mobileChrome", true);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(dp(4));

    m_search = new QLineEdit;
    m_search->setProperty("mobileChrome", true);
    m_search->setPlaceholderText(i18n("Search commands"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(
        mobileui::icon(QStringLiteral("search"), Theme::current().textDim),
        QLineEdit::LeadingPosition);
    applyFont(m_search, TextRole::Body);
    QHBoxLayout *searchLayout = new QHBoxLayout;
    searchLayout->setContentsMargins(dp(16), 0, dp(16), dp(4));
    searchLayout->addWidget(m_search);
    layout->addLayout(searchLayout);
    connect(
        m_search, &QLineEdit::textChanged, this,
        &CommandBrowser::updateSearch);

    m_stack = new QStackedWidget;
    layout->addWidget(m_stack, 1);
}

void CommandBrowser::setHiddenActionNames(const QStringList &names)
{
    m_hiddenActionNames = names;
}

void CommandBrowser::setActionSource(const std::function<QList<QAction *>()> &source)
{
    m_actionSource = source;
}

void CommandBrowser::setQuickActions(const QVector<QAction *> &actions)
{
    m_quickActions.clear();
    for(QAction *action : actions) {
        if(action) {
            m_quickActions.append(action);
        }
    }
}

QVector<CommandBrowser::Entry> CommandBrowser::allEntries() const
{
    QVector<Entry> entries;
    QSet<QAction *> seen;
    if(m_menuBar) {
        for(QAction *top : m_menuBar->actions()) {
            if(QMenu *menu = top->menu()) {
                collectEntries(menu, stripMnemonic(menu->title()), entries);
            }
        }
    }
    for(const Entry &entry : entries) {
        seen.insert(entry.action);
    }
    const QMap<QString, QList<QAction *>> categories = actionsByCategory();
    for(auto it = categories.constBegin(); it != categories.constEnd(); ++it) {
        for(QAction *action : it.value()) {
            if(!seen.contains(action)) {
                seen.insert(action);
                entries.append({action, it.key(), false});
            }
        }
    }
    return entries;
}

QMap<QString, QList<QAction *>> CommandBrowser::actionsByCategory() const
{
    QMap<QString, QList<QAction *>> result;
    if(!m_actionSource) {
        return result;
    }
    QSet<QAction *> seen;
    for(QAction *action : m_actionSource()) {
        if(!action || seen.contains(action) || action->isSeparator() || !isListed(action) ||
           stripMnemonic(action->text()).isEmpty() || qobject_cast<QWidgetAction *>(action)) {
            continue;
        }
        seen.insert(action);
        KisActionRegistry::ActionCategory category =
            KisActionRegistry::instance()->fetchActionCategory(action->objectName());
        QString name = category.isValid() ? category.categoryName : QString();
        if(name.isEmpty()) {
            name = i18nc("action category", "Other");
        }
        result[stripMnemonic(name)].append(action);
    }
    for(auto it = result.begin(); it != result.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(), [](QAction *a, QAction *b) {
            return QString::localeAwareCompare(stripMnemonic(a->text()), stripMnemonic(b->text())) < 0;
        });
    }
    return result;
}

QWidget *CommandBrowser::makeNavigationRow(const QIcon &rowIcon, const QString &title, const QString &subtitle,
                                           const std::function<void()> &onClicked)
{
    CommandRow *row = new CommandRow(rowIcon, title, subtitle, CommandRow::Trailing::Chevron, false);
    connect(row, &CommandRow::clicked, this, onClicked);
    return row;
}

void CommandBrowser::pushCategories()
{
    QVBoxLayout *content;
    const QString title = i18n("All commands");
    QWidget *page = makePage(title, true, &content);
    QLabel *note = new QLabel(i18n("Every command of Krita, including the ones that only have a keyboard shortcut on the desktop."));
    note->setWordWrap(true);
    note->setProperty("mobileRole", QStringLiteral("dim"));
    note->setContentsMargins(dp(20), dp(4), dp(20), dp(8));
    applyFont(note, TextRole::Caption);
    content->addWidget(note);
    const QMap<QString, QList<QAction *>> categories = actionsByCategory();
    for(auto it = categories.constBegin(); it != categories.constEnd(); ++it) {
        const QString category = it.key();
        content->addWidget(makeNavigationRow(QIcon(), category,
                                             i18np("%1 command", "%1 commands", it.value().size()),
                                             [this, category] { pushCategory(category); }));
    }
    content->addStretch(1);
    m_pages.append({page, nullptr, title});
    m_stack->addWidget(page);
    m_stack->setCurrentWidget(page);
}

void CommandBrowser::pushCategory(const QString &category)
{
    QVBoxLayout *content;
    QWidget *page = makePage(category, true, &content);
    const QList<QAction *> actions = actionsByCategory().value(category);
    for(QAction *action : actions) {
        content->addWidget(makeRow(action, QString()));
    }
    content->addStretch(1);
    m_pages.append({page, nullptr, category});
    m_stack->addWidget(page);
    m_stack->setCurrentWidget(page);
}

void CommandBrowser::reset()
{
    hideMenusUpTo(0);
    {
        QSignalBlocker blocker(m_search);
        m_search->clear();
    }
    showRoot();
}

void CommandBrowser::leave()
{
    hideMenusUpTo(0);
}

void CommandBrowser::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if(m_pages.isEmpty()) {
        showRoot();
    }
}

void CommandBrowser::showRoot()
{
    clearPages();
    QVBoxLayout *content;
    QWidget *page = makePage(QString(), false, &content);

    // Quick actions as a grid of cards.
    QVector<QAction *> quick;
    for(const QPointer<QAction> &action : m_quickActions) {
        if(action && action->isVisible()) {
            quick.append(action);
        }
    }
    if(!quick.isEmpty()) {
        QWidget *grid = new QWidget;
        QGridLayout *gridLayout = new QGridLayout(grid);
        gridLayout->setContentsMargins(dp(16), dp(4), dp(16), dp(8));
        gridLayout->setSpacing(dp(8));
        int columns = qBound(3, (width() - dp(32)) / dp(100), 6);
        for(int i = 0; i < quick.size(); ++i) {
            QAction *action = quick[i];
            ActionCard *card = new ActionCard(
                QString(), stripMnemonic(action->text()));
            card->setCompact(true);
            card->setThemeIcon(action->icon());
            card->setEnabled(action->isEnabled());
            connect(card, &ActionCard::clicked, this, [this, action] {
                activate(action);
            });
            gridLayout->addWidget(card, i / columns, i % columns);
        }
        content->addWidget(grid);
        content->addWidget(makeDivider());
    }

    if(m_menuBar) {
        for(QAction *top : m_menuBar->actions()) {
            QMenu *menu = top->menu();
            if(menu && top->isVisible()) {
                content->addWidget(makeSubmenuRow(menu, true));
            }
        }
    }
    if(m_actionSource) {
        content->addWidget(makeDivider());
        content->addWidget(makeNavigationRow(mobileui::icon(QStringLiteral("search")), i18n("All commands"),
                                             i18n("Grouped by category"), [this] { pushCategories(); }));
    }
    content->addStretch(1);
    m_pages.append({page, nullptr, QString()});
    m_stack->addWidget(page);
    m_stack->setCurrentWidget(page);
}

void CommandBrowser::pushMenu(QMenu *menu, const QString &title)
{
    // Let the menu populate itself (e.g. recent files) and apply permission
    // state, just like it would before popping up on the desktop.
    Q_EMIT menu->aboutToShow();
    m_shownMenus.append(menu);

    QVBoxLayout *content;
    QWidget *page = makePage(title, true, &content);
    addMenuRows(content, menu);
    content->addStretch(1);
    m_pages.append({page, menu, title});
    m_stack->addWidget(page);
    m_stack->setCurrentWidget(page);
}

void CommandBrowser::popPage()
{
    if(m_pages.size() <= 1) {
        return;
    }
    Page page = m_pages.takeLast();
    if(page.menu) {
        hideMenusUpTo(m_shownMenus.size() - 1);
    }
    m_stack->removeWidget(page.widget);
    page.widget->deleteLater();
    m_stack->setCurrentWidget(m_pages.last().widget);
}

void CommandBrowser::updateSearch(const QString &text)
{
    QString query = text.trimmed();
    if(query.isEmpty()) {
        reset();
        return;
    }

    // Drop everything above the root and replace it with a results page.
    while(m_pages.size() > 1) {
        popPage();
    }
    QVBoxLayout *content;
    QWidget *page = makePage(QString(), false, &content);
    int count = 0;
    for(const Entry &entry : allEntries()) {
        QString label = stripMnemonic(entry.action->text());
        if(label.contains(query, Qt::CaseInsensitive) ||
           entry.path.contains(query, Qt::CaseInsensitive)) {
            content->addWidget(makeRow(entry.action, entry.path));
            if(++count >= 80) {
                break;
            }
        }
    }
    if(count == 0) {
        QLabel *none = new QLabel(i18n("No matching commands."));
        none->setProperty("mobileRole", QStringLiteral("dim"));
        none->setAlignment(Qt::AlignCenter);
        none->setMinimumHeight(dp(80));
        applyFont(none, TextRole::Body);
        content->addWidget(none);
    }
    content->addStretch(1);
    m_pages.append({page, nullptr, QString()});
    m_stack->addWidget(page);
    m_stack->setCurrentWidget(page);
}

QWidget *CommandBrowser::makePage(
    const QString &title, bool withBack, QVBoxLayout **outContent)
{
    QWidget *page = new QWidget;
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    if(withBack) {
        QWidget *header = new QWidget;
        QHBoxLayout *headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(dp(8), 0, dp(16), 0);
        headerLayout->setSpacing(dp(4));
        ChromeButton *back =
            new ChromeButton(QStringLiteral("back"), i18n("Back"));
        connect(
            back, &ChromeButton::clicked, this, &CommandBrowser::popPage);
        headerLayout->addWidget(back);
        QLabel *label = new QLabel(title);
        label->setProperty("mobileRole", QStringLiteral("title"));
        applyFont(label, TextRole::Subtitle, true);
        headerLayout->addWidget(label, 1);
        pageLayout->addWidget(header);
    }

    QScrollArea *scroll = new QScrollArea;
    scroll->setProperty("mobileChrome", true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QWidget *inner = new QWidget;
    QVBoxLayout *content = new QVBoxLayout(inner);
    content->setContentsMargins(0, 0, 0, dp(16));
    content->setSpacing(0);
    scroll->setWidget(inner);
    enableKineticScrolling(scroll);
    pageLayout->addWidget(scroll, 1);

    *outContent = content;
    return page;
}

void CommandBrowser::addMenuRows(QVBoxLayout *layout, QMenu *menu)
{
    bool pendingDivider = false;
    bool haveRows = false;
    for(QAction *action : menu->actions()) {
        if(action->isSeparator()) {
            pendingDivider = haveRows;
            continue;
        }
        if(!action->isVisible() || !isListed(action)) {
            continue;
        }
        if(pendingDivider) {
            layout->addWidget(makeDivider());
            pendingDivider = false;
        }
        if(QMenu *submenu = action->menu()) {
            layout->addWidget(makeSubmenuRow(submenu, false));
        } else if(qobject_cast<QWidgetAction *>(action)) {
            // Embedded widgets can't be shown as rows, offer the classic
            // popup menu for these instead.
            CommandRow *row = new CommandRow(
                mobileui::icon(QStringLiteral("classic")), i18n("Show classic menu"),
                QString(), CommandRow::Trailing::None, false);
            connect(row, &CommandRow::clicked, this, [this, menu] {
                QPointer<QMenu> m = menu;
                hideMenusUpTo(0);
                Q_EMIT closeRequested();
                QTimer::singleShot(0, this, [this, m] {
                    if(m) {
                        m->popup(mapToGlobal(rect().center()));
                    }
                });
            });
            layout->addWidget(row);
        } else {
            layout->addWidget(makeRow(action, QString()));
        }
        haveRows = true;
    }
    if(!haveRows) {
        QLabel *none = new QLabel(i18n("Nothing here right now."));
        none->setProperty("mobileRole", QStringLiteral("dim"));
        none->setAlignment(Qt::AlignCenter);
        none->setMinimumHeight(dp(80));
        applyFont(none, TextRole::Body);
        layout->addWidget(none);
    }
}

QWidget *CommandBrowser::makeRow(QAction *action, const QString &subtitle)
{
    CommandRow::Trailing trailing = CommandRow::Trailing::None;
    if(action->isCheckable()) {
        QActionGroup *group = action->actionGroup();
        if(group && group->isExclusive()) {
            trailing = CommandRow::Trailing::Radio;
        } else {
            trailing = CommandRow::Trailing::Switch;
        }
    }
    CommandRow *row = new CommandRow(
        action->icon(), stripMnemonic(action->text()), subtitle, trailing,
        action->isChecked());
    row->setEnabled(action->isEnabled());
    QString tip = action->statusTip().isEmpty() ? action->toolTip()
                                                : action->statusTip();
    row->setToolTip(tip);
    QPointer<QAction> guarded = action;
    connect(row, &CommandRow::clicked, this, [this, guarded] {
        if(guarded) {
            activate(guarded);
        }
    });
    return row;
}

QWidget *CommandBrowser::makeSubmenuRow(QMenu *menu, bool topLevel)
{
    QString title = stripMnemonic(menu->title());
    QIcon rowIcon = menu->icon();
    if(rowIcon.isNull() && topLevel) {
        rowIcon = mobileui::icon(menuIconFor(menu));
    }
    CommandRow *row = new CommandRow(
        rowIcon, title, QString(), CommandRow::Trailing::Chevron, false);
    row->setEnabled(menu->menuAction()->isEnabled());
    QPointer<QMenu> guarded = menu;
    connect(row, &CommandRow::clicked, this, [this, guarded, title] {
        if(guarded) {
            pushMenu(guarded, title);
        }
    });
    return row;
}

void CommandBrowser::activate(QAction *action)
{
    if(!action->isEnabled()) {
        return;
    }

    if(action->isCheckable() && !m_search->text().trimmed().isEmpty()) {
        // Toggle in place inside search results.
        action->trigger();
        updateSearch(m_search->text());
        return;
    }

    if(action->isCheckable()) {
        action->trigger();
        // Rebuild the current page so the indicators reflect the new state,
        // keeping the scroll position.
        if(!m_pages.isEmpty() && !m_pages.last().menu && m_pages.size() > 1) {
            // Category page: rebuild it in place.
            Page page = m_pages.takeLast();
            m_stack->removeWidget(page.widget);
            page.widget->deleteLater();
            if(page.title == i18n("All commands")) {
                pushCategories();
            } else {
                pushCategory(page.title);
            }
            return;
        }
        if(!m_pages.isEmpty() && m_pages.last().menu) {
            Page page = m_pages.last();
            QScrollArea *scroll = page.widget->findChild<QScrollArea *>();
            int pos = scroll ? scroll->verticalScrollBar()->value() : 0;
            QVBoxLayout *content;
            QWidget *newPage = makePage(page.title, true, &content);
            addMenuRows(content, page.menu);
            content->addStretch(1);
            m_stack->addWidget(newPage);
            m_stack->setCurrentWidget(newPage);
            m_stack->removeWidget(page.widget);
            page.widget->deleteLater();
            m_pages.last().widget = newPage;
            if(QScrollArea *newScroll = newPage->findChild<QScrollArea *>()) {
                QTimer::singleShot(0, newScroll, [newScroll, pos] {
                    newScroll->verticalScrollBar()->setValue(pos);
                });
            }
        }
        return;
    }

    // Like a real menu: hide first, then trigger the action.
    QPointer<QAction> guarded = action;
    hideMenusUpTo(0);
    Q_EMIT closeRequested();
    QTimer::singleShot(0, this, [guarded] {
        if(guarded) {
            guarded->trigger();
        }
    });
}

bool CommandBrowser::isListed(QAction *action) const
{
    return !m_hiddenActionNames.contains(action->objectName());
}

void CommandBrowser::collectEntries(
    QMenu *menu, const QString &path, QVector<Entry> &out) const
{
    for(QAction *action : menu->actions()) {
        if(action->isSeparator() || !action->isVisible() || !isListed(action)) {
            continue;
        }
        if(QMenu *submenu = action->menu()) {
            collectEntries(
                submenu,
                QStringLiteral("%1 › %2").arg(
                    path, stripMnemonic(submenu->title())),
                out);
        } else if(!qobject_cast<QWidgetAction *>(action)) {
            out.append({action, path, true});
        }
    }
}

void CommandBrowser::clearPages()
{
    for(const Page &page : m_pages) {
        m_stack->removeWidget(page.widget);
        page.widget->deleteLater();
    }
    m_pages.clear();
}

void CommandBrowser::hideMenusUpTo(int depth)
{
    while(m_shownMenus.size() > depth) {
        QPointer<QMenu> menu = m_shownMenus.takeLast();
        if(menu) {
            Q_EMIT menu->aboutToHide();
        }
    }
}

}
