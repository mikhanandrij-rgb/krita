/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Krita Mobile (unofficial fork): top bar, tool rail and quick sliders.
#ifndef MOBILE_CHROME_H
#define MOBILE_CHROME_H

#include <QIcon>
#include <QAction>
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <QWidget>

class KisDoubleSliderSpinBox;
class QAction;
class QBoxLayout;
class QLabel;

namespace mobileui {

class ChromeButton;

// One of Krita's tools, as shown in the rail and the tool drawer.
struct ToolEntry {
    QString id;       // KoToolFactory id, also the name of its action
    QString name;     // translated, for labels
    QIcon icon;
    QString section;  // Krita's tool box section
    int priority = 0; // order within the section
};

// Compact bar at the top of the editor, replacing the desktop menu bar and
// tool bars.
class TopBar final : public QWidget
{
    Q_OBJECT
public:
    explicit TopBar(QAction *undo, QAction *redo, QWidget *parent = nullptr);

    void setTitle(const QString &title, bool dirty);
    void setMenuChecked(bool checked);
    void setMoreChecked(bool checked);
    // In landscape, the panel buttons move from the tool rail to the top bar
    // so the vertical rail has room for more tools.
    void setShowPanelButtons(bool show);
    void setPanelChecked(const QString &panelId);
    void setColors(const QColor &foreground, const QColor &background);

    QSize sizeHint() const override;

Q_SIGNALS:
    void projectsRequested();
    void menuRequested();
    void moreRequested();
    void layersRequested();
    void brushesRequested();
    void colorRequested();
    void animationRequested();
    void swapColorsRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    ChromeButton *m_projectsButton;
    ChromeButton *m_menuButton;
    QLabel *m_title;
    ChromeButton *m_undoButton;
    ChromeButton *m_redoButton;
    ChromeButton *m_moreButton;
    ChromeButton *m_layersButton;
    ChromeButton *m_brushesButton;
    ChromeButton *m_colorButton;
    ChromeButton *m_animationButton;
};

// Tool bar along the bottom (portrait) or the side (landscape). Shows the
// most recently used tools, a button for the tool drawer and shortcuts to the
// most important panels.
class ToolRail final : public QWidget
{
    Q_OBJECT
public:
    explicit ToolRail(const QVector<ToolEntry> &tools, QWidget *parent = nullptr);

    void setOrientation(Qt::Orientation orientation);
    Qt::Orientation orientation() const { return m_orientation; }
    void setSlotCount(int count);
    int slotCount() const { return m_slotCount; }
    void setColors(const QColor &foreground, const QColor &background);
    void setPanelChecked(const QString &panelId);
    void setLeftHanded(bool leftHanded);
    void setShowPanelButtons(bool show);
    void setActiveTool(const QString &toolId);

    QSize sizeHint() const override;

Q_SIGNALS:
    void toolRequested(const QString &toolId);
    void toolSettingsRequested();
    void drawerRequested();
    void layersRequested();
    void brushesRequested();
    void colorRequested();
    void animationRequested();
    void swapColorsRequested();

private:
    void rebuild();
    void loadSlots();
    void saveSlots() const;
    const ToolEntry *toolById(const QString &id) const;
    void noteToolUsed(const QString &toolId);

    QVector<ToolEntry> m_tools;
    QStringList m_slots;  // tool ids in the rail
    QVector<int> m_usage; // last-use counter per slot
    int m_usageCounter = 0;
    int m_slotCount = 4;
    QString m_activeTool;
    Qt::Orientation m_orientation = Qt::Horizontal;
    bool m_leftHanded = false;
    bool m_showPanelButtons = true;
    QBoxLayout *m_layout;
    QVector<ChromeButton *> m_slotButtons;
    ChromeButton *m_drawerButton;
    ChromeButton *m_layersButton;
    ChromeButton *m_brushesButton;
    ChromeButton *m_colorButton;
    ChromeButton *m_animationButton;
    QWidget *m_separator;
};

// One vertical slider that adjusts a value by relative dragging. The value
// itself stays in Krita's own slider (the brush size or opacity slider of the
// brush tool bar), which remains the single source of truth.
class QuickSlider final : public QWidget
{
    Q_OBJECT
public:
    enum class Kind { Size, Opacity };
    explicit QuickSlider(Kind kind, QWidget *parent = nullptr);

    void setTarget(KisDoubleSliderSpinBox *target);
    KisDoubleSliderSpinBox *target() const;
    void setLength(int length);
    QSize sizeHint() const override;

Q_SIGNALS:
    void dragStateChanged(bool dragging, const QString &label);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    qreal fraction() const;
    void setFraction(qreal fraction);
    qreal sizeMaximum() const;
    QString valueLabel() const;

    Kind m_kind;
    QPointer<KisDoubleSliderSpinBox> m_target;
    int m_length = 160;
    bool m_dragging = false;
    int m_dragStartY = 0;
    qreal m_dragStartFraction = 0.0;
    qreal m_dragMaximum = 0.0;
};

// Edge overlay with brush size and opacity sliders plus an eyedropper.
class QuickSliders final : public QWidget
{
    Q_OBJECT
public:
    explicit QuickSliders(QWidget *parent = nullptr);

    void setTargets(KisDoubleSliderSpinBox *size, KisDoubleSliderSpinBox *opacity);
    void fitHeight(int height);
    bool hasTargets() const;
    void setEyedropperChecked(bool checked);

Q_SIGNALS:
    void dragStateChanged(bool dragging, const QString &label);
    void eyedropperClicked();
    void eyedropperLongPressed();

private:
    QuickSlider *m_size;
    QuickSlider *m_opacity;
    ChromeButton *m_picker;
};

// Floating label shown next to the quick sliders while dragging.
class ValueBubble final : public QWidget
{
    Q_OBJECT
public:
    explicit ValueBubble(QWidget *parent = nullptr);
    void setText(const QString &text);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_text;
};

} // namespace mobileui

#endif
