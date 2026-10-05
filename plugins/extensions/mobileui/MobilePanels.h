/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Krita Mobile (unofficial fork): panels shown in the sheet that aren't Krita
// dockers: the tool drawer and the "more" panel.
#ifndef MOBILE_PANELS_H
#define MOBILE_PANELS_H

#include "MobileChrome.h"

#include <QPointer>
#include <QVector>
#include <QWidget>

class QAction;
class QGridLayout;
class QLabel;
class QVBoxLayout;

namespace mobileui {

class ActionCard;
class ChromeButton;

// Every one of Krita's tools, grouped like Krita's tool box, with labels.
class ToolDrawer final : public QWidget
{
    Q_OBJECT
public:
    explicit ToolDrawer(const QVector<ToolEntry> &tools, QWidget *parent = nullptr);

    void setActiveTool(const QString &toolId);

Q_SIGNALS:
    void toolChosen(const QString &toolId);
    void toolSettingsRequested(const QString &toolId);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Group {
        QGridLayout *grid;
        QVector<ChromeButton *> buttons;
    };
    void reflow();

    QVector<Group> m_groups;
    int m_columns = 0;
};

// A titled grid of cards, used to build the "more" panel.
class CardSection final : public QWidget
{
    Q_OBJECT
public:
    explicit CardSection(const QString &title, QWidget *parent = nullptr);
    ActionCard *addCard(const QString &iconName, const QString &title);
    // Card that mirrors and triggers an action; checkable actions are shown
    // with a highlighted state.
    ActionCard *addActionCard(QAction *action, const QString &iconName = QString());

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void reflow();

    QGridLayout *m_grid;
    QVector<ActionCard *> m_cards;
    int m_columns = 0;
};

class MorePanel final : public QWidget
{
    Q_OBJECT
public:
    explicit MorePanel(QWidget *parent = nullptr);
    CardSection *addSection(const QString &title);
    void addNote(const QString &text);

private:
    QVBoxLayout *m_layout;
};

} // namespace mobileui

#endif
