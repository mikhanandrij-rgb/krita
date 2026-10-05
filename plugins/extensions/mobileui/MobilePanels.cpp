/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Parts ported from the Drawpile Mobile fork (same author, GPL-3.0-or-later).
 */
#include "MobilePanels.h"
#include "MobileTheme.h"
#include "MobileWidgets.h"

#include <klocalizedstring.h>

#include <QAction>
#include <QGridLayout>
#include <QLabel>
#include <QMap>
#include <QVBoxLayout>

namespace mobileui {

namespace {
QLabel *makeSectionTitle(const QString &title)
{
    QLabel *label = new QLabel(title.toUpper());
    label->setProperty("mobileRole", QStringLiteral("dim"));
    applyFont(label, TextRole::Caption, true);
    label->setContentsMargins(dp(4), dp(12), dp(4), dp(4));
    return label;
}

// Krita's tool box sections (see ToolBoxSection in KoToolFactoryBase.h), in
// the order of the tool box.
QString sectionTitle(const QString &section)
{
    if (section.contains(QLatin1String("Krita/Shape"))) {
        return i18nc("tool group", "Paint and shapes");
    } else if (section == QLatin1String("main")) {
        return i18nc("tool group", "Vector and text");
    } else if (section.contains(QLatin1String("Krita/Transform"))) {
        return i18nc("tool group", "Transform");
    } else if (section.contains(QLatin1String("Krita/Fill"))) {
        return i18nc("tool group", "Fill");
    } else if (section.contains(QLatin1String("Krita/Select"))) {
        return i18nc("tool group", "Selection");
    } else if (section.contains(QLatin1String("Krita/View"))) {
        return i18nc("tool group", "Assistants and reference");
    } else if (section == QLatin1String("navigation")) {
        return i18nc("tool group", "Navigation");
    }
    return i18nc("tool group", "Other");
}

int sectionOrder(const QString &section)
{
    static const char *order[] = {"Krita/Shape", "main", "Krita/Transform", "Krita/Fill",
                                  "Krita/Select", "Krita/View", "navigation"};
    for (int i = 0; i < int(sizeof(order) / sizeof(order[0])); ++i) {
        if (section.contains(QLatin1String(order[i]))) {
            return i;
        }
    }
    return 100;
}
} // namespace

// ---------------------------------------------------------------------------
// ToolDrawer

ToolDrawer::ToolDrawer(const QVector<ToolEntry> &tools, QWidget *parent)
    : QWidget(parent)
{
    setProperty("mobileChrome", true);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(dp(12), 0, dp(12), dp(12));
    layout->setSpacing(dp(4));

    // Group by section, keeping Krita's priorities inside each group.
    QMap<int, QPair<QString, QVector<ToolEntry>>> groups;
    for (const ToolEntry &tool : tools) {
        auto &group = groups[sectionOrder(tool.section)];
        group.first = sectionTitle(tool.section);
        group.second.append(tool);
    }

    for (auto it = groups.begin(); it != groups.end(); ++it) {
        QVector<ToolEntry> entries = it.value().second;
        std::stable_sort(entries.begin(), entries.end(), [](const ToolEntry &a, const ToolEntry &b) {
            return a.priority < b.priority;
        });
        layout->addWidget(makeSectionTitle(it.value().first));
        QWidget *gridWidget = new QWidget;
        Group group;
        group.grid = new QGridLayout(gridWidget);
        group.grid->setContentsMargins(0, 0, 0, 0);
        group.grid->setHorizontalSpacing(dp(4));
        group.grid->setVerticalSpacing(dp(8));
        for (const ToolEntry &tool : entries) {
            ChromeButton *button = new ChromeButton;
            button->setShowLabel(true);
            button->setButtonSize(52);
            button->setThemeIcon(tool.icon);
            button->setText(tool.name);
            button->setToolTip(tool.name);
            button->setAccessibleName(tool.name);
            button->setCheckable(true);
            button->setProperty("toolId", tool.id);
            const QString id = tool.id;
            connect(button, &ChromeButton::clicked, this, [this, id] {
                Q_EMIT toolChosen(id);
            });
            connect(button, &ChromeButton::longPressed, this, [this, id] {
                Q_EMIT toolSettingsRequested(id);
            });
            group.buttons.append(button);
        }
        m_groups.append(group);
        layout->addWidget(gridWidget);
    }

    QLabel *hint = new QLabel(i18n("Tap a tool to use it. Tap the active tool in the bar again, or long-press any tool, to open its options."));
    hint->setWordWrap(true);
    hint->setProperty("mobileRole", QStringLiteral("dim"));
    applyFont(hint, TextRole::Caption);
    hint->setContentsMargins(dp(4), dp(8), dp(4), 0);
    layout->addWidget(hint);
    layout->addStretch(1);
    reflow();
}

void ToolDrawer::setActiveTool(const QString &toolId)
{
    for (const Group &group : m_groups) {
        for (ChromeButton *button : group.buttons) {
            button->setChecked(button->property("toolId").toString() == toolId);
        }
    }
}

void ToolDrawer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    reflow();
}

void ToolDrawer::reflow()
{
    const int columns = qBound(3, (width() - dp(24)) / dp(80), 8);
    if (columns == m_columns) {
        return;
    }
    m_columns = columns;
    for (Group &group : m_groups) {
        for (ChromeButton *button : group.buttons) {
            group.grid->removeWidget(button);
        }
        for (int i = 0; i < group.buttons.size(); ++i) {
            group.grid->addWidget(group.buttons[i], i / columns, i % columns);
        }
        for (int c = 0; c < 8; ++c) {
            group.grid->setColumnStretch(c, c < columns ? 1 : 0);
        }
    }
}

// ---------------------------------------------------------------------------
// CardSection

CardSection::CardSection(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    setProperty("mobileChrome", true);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(dp(4));
    if (!title.isEmpty()) {
        layout->addWidget(makeSectionTitle(title));
    }
    QWidget *gridWidget = new QWidget;
    m_grid = new QGridLayout(gridWidget);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(dp(8));
    layout->addWidget(gridWidget);
}

ActionCard *CardSection::addCard(const QString &iconName, const QString &title)
{
    ActionCard *card = new ActionCard(iconName, title);
    card->setCompact(true);
    m_cards.append(card);
    m_columns = 0;
    reflow();
    return card;
}

ActionCard *CardSection::addActionCard(QAction *action, const QString &iconName)
{
    ActionCard *card = addCard(iconName, stripMnemonic(action->text()));
    if (iconName.isEmpty()) {
        card->setThemeIcon(action->icon());
    }
    auto sync = [card, action] {
        card->setEnabled(action->isEnabled());
        card->setCheckable(action->isCheckable());
        if (action->isCheckable()) {
            card->setChecked(action->isChecked());
        }
        card->update();
    };
    sync();
    connect(action, &QAction::changed, card, sync);
    connect(action, &QAction::toggled, card, sync);
    connect(card, &ActionCard::clicked, action, [action, sync] {
        action->trigger();
        sync();
    });
    return card;
}

void CardSection::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    reflow();
}

void CardSection::reflow()
{
    const int columns = qBound(3, width() / dp(96), 6);
    if (columns == m_columns) {
        return;
    }
    m_columns = columns;
    for (ActionCard *card : m_cards) {
        m_grid->removeWidget(card);
    }
    for (int i = 0; i < m_cards.size(); ++i) {
        m_grid->addWidget(m_cards[i], i / columns, i % columns);
    }
    for (int c = 0; c < 8; ++c) {
        m_grid->setColumnStretch(c, c < columns ? 1 : 0);
    }
}

// ---------------------------------------------------------------------------
// MorePanel

MorePanel::MorePanel(QWidget *parent)
    : QWidget(parent)
{
    setProperty("mobileChrome", true);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(dp(16), 0, dp(16), dp(16));
    m_layout->setSpacing(dp(8));
    m_layout->addStretch(1);
}

CardSection *MorePanel::addSection(const QString &title)
{
    CardSection *section = new CardSection(title);
    m_layout->insertWidget(m_layout->count() - 1, section);
    return section;
}

void MorePanel::addNote(const QString &text)
{
    QLabel *label = new QLabel(text);
    label->setWordWrap(true);
    label->setProperty("mobileRole", QStringLiteral("dim"));
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setOpenExternalLinks(true);
    applyFont(label, TextRole::Caption);
    label->setContentsMargins(dp(4), dp(12), dp(4), 0);
    m_layout->insertWidget(m_layout->count() - 1, label);
}

} // namespace mobileui
