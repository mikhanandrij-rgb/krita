/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Ported from the Drawpile Mobile fork (same author, GPL-3.0-or-later) and
 * extended with Krita's action categories.
 */
// Krita Mobile (unofficial fork): touch-friendly browser for all main menu commands.
#ifndef MOBILE_COMMAND_BROWSER_H
#define MOBILE_COMMAND_BROWSER_H
#include <QPointer>
#include <QStringList>
#include <functional>
#include <QVector>
#include <QMap>
#include <QWidget>

class QAction;
class QIcon;
class QLineEdit;
class QMenu;
class QMenuBar;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

namespace mobileui {

// Shows the main window's menu bar as a hierarchy of large rows, every other
// action grouped by Krita's action categories (many of Krita's actions are
// only reachable through shortcuts on the desktop), and a search over all of
// them. It is generated from the live menus and action collections every
// time it's opened, so nothing can become unreachable and new upstream
// actions show up automatically.
class CommandBrowser final : public QWidget {
    Q_OBJECT
public:
    CommandBrowser(QMenuBar *menuBar, QWidget *parent = nullptr);

    // Actions that should not be listed because the mobile layout replaces
    // them (e.g. toggles for the classic small-screen tool bars).
    void setHiddenActionNames(const QStringList &names);
    // Quick actions shown as cards on the root page.
    void setQuickActions(const QVector<QAction *> &actions);
    // Provides every action of the window (all action collections).
    void setActionSource(const std::function<QList<QAction *>()> &source);

    // Flattened list of every reachable action with its breadcrumb, used by
    // the action inventory dump.
    struct Entry {
        QAction *action;
        QString path;
        bool inMenu;
    };
    QVector<Entry> allEntries() const;

    // Activates an action exactly like tapping its row does. Used by the
    // automated smoke test.
    void activateForTest(QAction *action) { activate(action); }

public Q_SLOTS:
    void reset();
    void leave();

Q_SIGNALS:
    void closeRequested();

protected:
    void showEvent(QShowEvent *event) override;

private:
    struct Page {
        QWidget *widget;
        QMenu *menu; // null for the root and search pages
        QString title;
    };

    void showRoot();
    void pushMenu(QMenu *menu, const QString &title);
    void pushCategories();
    void pushCategory(const QString &category);
    QMap<QString, QList<QAction *>> actionsByCategory() const;
    QWidget *makeNavigationRow(const QIcon &icon, const QString &title, const QString &subtitle,
                               const std::function<void()> &onClicked);
    void popPage();
    void updateSearch(const QString &text);
    QWidget *makePage(
        const QString &title, bool withBack, QVBoxLayout **outContent);
    void addMenuRows(QVBoxLayout *layout, QMenu *menu);
    QWidget *makeRow(QAction *action, const QString &subtitle);
    QWidget *makeSubmenuRow(QMenu *menu, bool topLevel);
    void activate(QAction *action);
    bool isListed(QAction *action) const;
    void collectEntries(
        QMenu *menu, const QString &path, QVector<Entry> &out) const;
    void clearPages();
    void hideMenusUpTo(int depth);

    QPointer<QMenuBar> m_menuBar;
    QLineEdit *m_search;
    QStackedWidget *m_stack;
    QVector<Page> m_pages;
    QVector<QPointer<QMenu>> m_shownMenus;
    QStringList m_hiddenActionNames;
    QVector<QPointer<QAction>> m_quickActions;
    std::function<QList<QAction *>()> m_actionSource;
};

}

#endif
