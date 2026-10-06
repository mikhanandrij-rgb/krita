/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Krita Mobile (unofficial fork): project hub shown at startup (instead of
// Krita's welcome page) and through the projects button.
#ifndef MOBILE_HUB_H
#define MOBILE_HUB_H

#include <QColor>
#include <QPointer>
#include <QUrl>
#include <QVector>
#include <QWidget>

#include <functional>

class KisMainWindow;
class QButtonGroup;
class QGridLayout;
class QLabel;
class QScrollArea;
class QSpinBox;
class QStackedWidget;

namespace mobileui {

class ActionCard;
class RecentCard;

class Hub final : public QWidget
{
    Q_OBJECT
public:
    Hub(KisMainWindow *mainWindow, QWidget *parent);

    void showMainPage();
    void showNewPage();
    // Android back button. Returns true if the hub handled it.
    bool handleBack(bool release);

    // Used by the automated screenshot run.
    bool isOnNewPage() const;
    void createCanvasForTest();

Q_SIGNALS:
    void closeRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    struct Preset {
        QString iconName;
        QString title;
        QSize size;
        int dpi;
        bool animation;
    };

    QWidget *buildMainPage();
    QWidget *buildNewPage();
    void refreshRecents();
    void refreshContinueCard();
    void refreshRecovery();
    void reflowGrids();
    void showRecentActions(const QUrl &url);
    void showChoices(const QString &title,
                     const QVector<QPair<QString, QString>> &choices,
                     const std::function<void(int)> &onChosen);
    void openUrl(const QUrl &url);
    void createCanvas();
    void selectPreset(int index);
    void updateNewPageSummary();
    void triggerAction(const QString &name);
    void expectDocumentLoad();
    QSize phoneSize(bool portrait) const;
    QStringList autosaveFiles() const;
    QString autosaveLocation() const;

    QPointer<KisMainWindow> m_mainWindow;
    QStackedWidget *m_pages;
    QWidget *m_mainPage;
    QWidget *m_newPage;
    QWidget *m_continueCard;
    QLabel *m_continueThumb;
    QLabel *m_continueTitle;
    QLabel *m_continueMeta;
    QGridLayout *m_actionGrid;
    QVector<ActionCard *> m_actionCards;
    ActionCard *m_recoverCard;
    QGridLayout *m_recentGrid;
    QVector<RecentCard *> m_recentCards;
    QLabel *m_recentEmpty;
    int m_columns = 0;
    int m_actionColumns = 0;

    QVector<Preset> m_presets;
    QVector<ActionCard *> m_presetCards;
    QGridLayout *m_presetGrid;
    int m_presetColumns = 0;
    QWidget *m_fpsRow;
    QButtonGroup *m_fpsGroup;
    QSpinBox *m_widthBox;
    QSpinBox *m_heightBox;
    QSpinBox *m_dpiBox;
    QButtonGroup *m_backgroundGroup;
    QColor m_customBackground = Qt::white;
    QLabel *m_summary;
    int m_selectedPreset = -1;
    bool m_expectLoad = false;
};

} // namespace mobileui

#endif
