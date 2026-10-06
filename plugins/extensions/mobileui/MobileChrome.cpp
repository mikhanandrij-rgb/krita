/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Parts ported from the Drawpile Mobile fork (same author, GPL-3.0-or-later).
 */
#include "MobileChrome.h"
#include "MobileConfig.h"
#include "MobileTheme.h"
#include "MobileWidgets.h"

#include <kis_slider_spin_box.h>
#include <klocalizedstring.h>

#include <QAction>
#include <QBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

namespace mobileui {

namespace {
QPoint eventPos(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

ChromeButton *panelButton(const QString &iconName, const QString &text)
{
    ChromeButton *button = new ChromeButton(iconName, text);
    button->setCheckable(true);
    return button;
}
} // namespace

// ---------------------------------------------------------------------------
// TopBar

TopBar::TopBar(QAction *undo, QAction *redo, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mobileTopBar"));
    setProperty("mobileChrome", true);
    setAttribute(Qt::WA_StyledBackground, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(dp(4), dp(2), dp(4), dp(2));
    layout->setSpacing(0);

    m_projectsButton = new ChromeButton(QStringLiteral("projects"), i18n("Projects"));
    connect(m_projectsButton, &ChromeButton::clicked, this, &TopBar::projectsRequested);
    layout->addWidget(m_projectsButton);

    m_menuButton = new ChromeButton(QStringLiteral("menu"), i18n("Menu"));
    m_menuButton->setCheckable(true);
    connect(m_menuButton, &ChromeButton::clicked, this, &TopBar::menuRequested);
    layout->addWidget(m_menuButton);

    m_title = new QLabel;
    m_title->setProperty("mobileRole", QStringLiteral("title"));
    m_title->setMinimumWidth(dp(40));
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_title->installEventFilter(this);
    applyFont(m_title, TextRole::Body, true);
    layout->addSpacing(dp(4));
    layout->addWidget(m_title, 1);

    m_undoButton = new ChromeButton(QStringLiteral("undo"), i18n("Undo"));
    m_undoButton->bindAction(undo);
    layout->addWidget(m_undoButton);

    m_redoButton = new ChromeButton(QStringLiteral("redo"), i18n("Redo"));
    m_redoButton->bindAction(redo);
    layout->addWidget(m_redoButton);

    m_brushesButton = panelButton(QStringLiteral("brush"), i18n("Brushes"));
    m_brushesButton->hide();
    connect(m_brushesButton, &ChromeButton::clicked, this, &TopBar::brushesRequested);
    layout->addWidget(m_brushesButton);

    m_layersButton = panelButton(QStringLiteral("layers"), i18n("Layers"));
    m_layersButton->hide();
    connect(m_layersButton, &ChromeButton::clicked, this, &TopBar::layersRequested);
    layout->addWidget(m_layersButton);

    m_animationButton = panelButton(QStringLiteral("timeline"), i18n("Animation"));
    m_animationButton->hide();
    connect(m_animationButton, &ChromeButton::clicked, this, &TopBar::animationRequested);
    layout->addWidget(m_animationButton);

    m_colorButton = panelButton(QString(), i18n("Color"));
    m_colorButton->setSwatch(Qt::black, Qt::white);
    m_colorButton->hide();
    connect(m_colorButton, &ChromeButton::clicked, this, &TopBar::colorRequested);
    connect(m_colorButton, &ChromeButton::longPressed, this, &TopBar::swapColorsRequested);
    layout->addWidget(m_colorButton);

    m_moreButton = new ChromeButton(QStringLiteral("more"), i18n("More"));
    m_moreButton->setCheckable(true);
    connect(m_moreButton, &ChromeButton::clicked, this, &TopBar::moreRequested);
    layout->addWidget(m_moreButton);
}

void TopBar::setTitle(const QString &title, bool dirty)
{
    const QString text = dirty ? QStringLiteral("%1 •").arg(title) : title;
    m_title->setProperty("fullText", text);
    m_title->setToolTip(title);
    QFontMetrics fm(m_title->font());
    m_title->setText(fm.elidedText(text, Qt::ElideMiddle, m_title->width()));
}

void TopBar::setMenuChecked(bool checked)
{
    m_menuButton->setChecked(checked);
}

void TopBar::setMoreChecked(bool checked)
{
    m_moreButton->setChecked(checked);
}

void TopBar::setShowPanelButtons(bool show)
{
    m_layersButton->setVisible(show);
    m_brushesButton->setVisible(show);
    m_animationButton->setVisible(show);
    m_colorButton->setVisible(show);
}

void TopBar::setPanelChecked(const QString &panelId)
{
    m_layersButton->setChecked(panelId == QLatin1String("layers"));
    m_brushesButton->setChecked(panelId == QLatin1String("brush"));
    m_animationButton->setChecked(panelId == QLatin1String("animation"));
    m_colorButton->setChecked(panelId == QLatin1String("color"));
}

void TopBar::setColors(const QColor &foreground, const QColor &background)
{
    m_colorButton->setSwatch(foreground, background);
}

QSize TopBar::sizeHint() const
{
    return QSize(dp(360), dp(52));
}

bool TopBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_title) {
        if (event->type() == QEvent::MouseButtonRelease) {
            Q_EMIT menuRequested();
            return true;
        } else if (event->type() == QEvent::Resize) {
            const QString text = m_title->property("fullText").toString();
            QFontMetrics fm(m_title->font());
            m_title->setText(fm.elidedText(text, Qt::ElideMiddle, m_title->width()));
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// ToolRail

ToolRail::ToolRail(const QVector<ToolEntry> &tools, QWidget *parent)
    : QWidget(parent)
    , m_tools(tools)
{
    setObjectName(QStringLiteral("mobileToolRail"));
    setProperty("mobileChrome", true);
    setAttribute(Qt::WA_StyledBackground, true);

    m_layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    m_layout->setContentsMargins(dp(4), dp(4), dp(4), dp(4));
    m_layout->setSpacing(0);

    m_drawerButton = new ChromeButton(QStringLiteral("drawer"), i18n("All tools"));
    m_drawerButton->setCheckable(true);
    connect(m_drawerButton, &ChromeButton::clicked, this, &ToolRail::drawerRequested);

    m_brushesButton = panelButton(QStringLiteral("brush"), i18n("Brushes"));
    connect(m_brushesButton, &ChromeButton::clicked, this, &ToolRail::brushesRequested);

    m_layersButton = panelButton(QStringLiteral("layers"), i18n("Layers"));
    connect(m_layersButton, &ChromeButton::clicked, this, &ToolRail::layersRequested);

    m_animationButton = panelButton(QStringLiteral("timeline"), i18n("Animation"));
    connect(m_animationButton, &ChromeButton::clicked, this, &ToolRail::animationRequested);

    m_colorButton = panelButton(QString(), i18n("Color"));
    m_colorButton->setSwatch(Qt::black, Qt::white);
    connect(m_colorButton, &ChromeButton::clicked, this, &ToolRail::colorRequested);
    connect(m_colorButton, &ChromeButton::longPressed, this, &ToolRail::swapColorsRequested);

    m_separator = new QWidget;
    m_separator->setAttribute(Qt::WA_StyledBackground, true);
    m_separator->setStyleSheet(QStringLiteral("background: %1;").arg(Theme::current().outline.name()));

    loadSlots();
    rebuild();
}

void ToolRail::setOrientation(Qt::Orientation orientation)
{
    if (orientation != m_orientation) {
        m_orientation = orientation;
        rebuild();
    }
}

void ToolRail::setSlotCount(int count)
{
    count = qBound(2, count, 10);
    if (count != m_slotCount) {
        m_slotCount = count;
        loadSlots();
        rebuild();
    }
}

void ToolRail::setColors(const QColor &foreground, const QColor &background)
{
    m_colorButton->setSwatch(foreground, background);
}

void ToolRail::setPanelChecked(const QString &panelId)
{
    m_drawerButton->setChecked(panelId == QLatin1String("tools"));
    m_layersButton->setChecked(panelId == QLatin1String("layers"));
    m_brushesButton->setChecked(panelId == QLatin1String("brush"));
    m_animationButton->setChecked(panelId == QLatin1String("animation"));
    m_colorButton->setChecked(panelId == QLatin1String("color"));
}

void ToolRail::setLeftHanded(bool leftHanded)
{
    if (leftHanded != m_leftHanded) {
        m_leftHanded = leftHanded;
        rebuild();
    }
}

void ToolRail::setShowPanelButtons(bool show)
{
    if (show != m_showPanelButtons) {
        m_showPanelButtons = show;
        rebuild();
    }
}

void ToolRail::setActiveTool(const QString &toolId)
{
    m_activeTool = toolId;
    noteToolUsed(toolId);
    for (ChromeButton *button : m_slotButtons) {
        button->setChecked(button->property("toolId").toString() == toolId);
    }
}

void ToolRail::noteToolUsed(const QString &toolId)
{
    if (!toolById(toolId)) {
        return;
    }
    int index = m_slots.indexOf(toolId);
    if (index == -1) {
        // Replace the least recently used slot in place, so the positions of
        // the other tools don't jump around.
        int lru = 0;
        for (int i = 1; i < m_usage.size(); ++i) {
            if (m_usage[i] < m_usage[lru]) {
                lru = i;
            }
        }
        if (lru < m_slots.size()) {
            m_slots[lru] = toolId;
            index = lru;
            if (lru < m_slotButtons.size()) {
                const ToolEntry *tool = toolById(toolId);
                ChromeButton *button = m_slotButtons[lru];
                button->setThemeIcon(tool->icon);
                button->setToolTip(tool->name);
                button->setAccessibleName(tool->name);
                button->setProperty("toolId", toolId);
            }
        }
    }
    if (index >= 0 && index < m_usage.size()) {
        m_usage[index] = ++m_usageCounter;
    }
    saveSlots();
}

QSize ToolRail::sizeHint() const
{
    const int thickness = dp(56);
    return m_orientation == Qt::Horizontal ? QSize(dp(360), thickness) : QSize(thickness, dp(360));
}

void ToolRail::rebuild()
{
    while (m_layout->count() > 0) {
        delete m_layout->takeAt(0);
    }
    for (ChromeButton *button : m_slotButtons) {
        // Hidden right away: a removed button would otherwise keep covering
        // the rail until it is deleted.
        button->hide();
        button->deleteLater();
    }
    m_slotButtons.clear();

    const bool horizontal = m_orientation == Qt::Horizontal;
    m_layout->setDirection(horizontal ? (m_leftHanded ? QBoxLayout::RightToLeft : QBoxLayout::LeftToRight)
                                      : QBoxLayout::TopToBottom);

    for (const QString &id : qAsConst(m_slots)) {
        const ToolEntry *tool = toolById(id);
        if (!tool) {
            continue;
        }
        ChromeButton *button = new ChromeButton(this);
        button->setThemeIcon(tool->icon);
        button->setToolTip(tool->name);
        button->setAccessibleName(tool->name);
        button->setCheckable(true);
        button->setChecked(id == m_activeTool);
        button->setProperty("toolId", id);
        // Tapping the tool that's already active opens its settings, like in
        // most mobile painting apps.
        connect(button, &ChromeButton::pressed, this, [button] {
            button->setProperty("wasActive", button->isChecked());
        });
        connect(button, &ChromeButton::clicked, this, [this, button] {
            const QString toolId = button->property("toolId").toString();
            if (button->property("wasActive").toBool()) {
                button->setChecked(true);
                Q_EMIT toolSettingsRequested();
            } else {
                Q_EMIT toolRequested(toolId);
            }
        });
        connect(button, &ChromeButton::longPressed, this, [this, button] {
            Q_EMIT toolRequested(button->property("toolId").toString());
            Q_EMIT toolSettingsRequested();
        });
        m_slotButtons.append(button);
        m_layout->addWidget(button);
    }
    m_layout->addWidget(m_drawerButton);
    m_layout->addStretch(1);
    if (horizontal) {
        m_separator->setFixedSize(1, dp(28));
    } else {
        m_separator->setFixedSize(dp(28), 1);
    }
    const QList<QWidget *> panels = {m_separator, m_brushesButton, m_colorButton, m_layersButton, m_animationButton};
    for (QWidget *w : panels) {
        w->setVisible(m_showPanelButtons);
    }
    if (m_showPanelButtons) {
        m_layout->addWidget(m_separator, 0, Qt::AlignCenter);
        m_layout->addWidget(m_brushesButton);
        m_layout->addWidget(m_colorButton);
        m_layout->addWidget(m_layersButton);
        m_layout->addWidget(m_animationButton);
    }
    updateGeometry();
}

void ToolRail::loadSlots()
{
    const QStringList saved = config::toolSlots();
    const QStringList defaults = {
        QStringLiteral("KritaShape/KisToolBrush"),
        QStringLiteral("KritaSelected/KisToolColorSampler"),
        QStringLiteral("KisToolSelectOutline"),
        QStringLiteral("KisToolTransform"),
        QStringLiteral("KritaFill/KisToolFill"),
        QStringLiteral("KritaTransform/KisToolMove"),
        QStringLiteral("KritaShape/KisToolLine"),
        QStringLiteral("KritaFill/KisToolGradient"),
        QStringLiteral("KisToolSelectRectangular"),
        QStringLiteral("SvgTextTool"),
    };
    QStringList result;
    for (const QString &id : saved + defaults) {
        if (result.size() >= m_slotCount) {
            break;
        }
        if (!result.contains(id) && toolById(id)) {
            result.append(id);
        }
    }
    m_slots = result;
    m_usage.fill(0, m_slots.size());
    // Earlier slots count as more recently used initially.
    for (int i = 0; i < m_usage.size(); ++i) {
        m_usage[i] = m_usage.size() - i;
    }
    m_usageCounter = m_usage.size();
}

void ToolRail::saveSlots() const
{
    const QStringList saved = config::toolSlots();
    // Keep entries beyond the current slot count, so rotating to a layout
    // with fewer slots and back doesn't lose them.
    QStringList merged = m_slots;
    for (const QString &id : saved) {
        if (!merged.contains(id)) {
            merged.append(id);
        }
    }
    config::setToolSlots(merged.mid(0, 10));
}

const ToolEntry *ToolRail::toolById(const QString &id) const
{
    for (const ToolEntry &tool : m_tools) {
        if (tool.id == id) {
            return &tool;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// QuickSlider

QuickSlider::QuickSlider(Kind kind, QWidget *parent)
    : QWidget(parent)
    , m_kind(kind)
{
    setProperty("mobileChrome", true);
    setAccessibleName(kind == Kind::Size ? i18n("Brush size") : i18n("Opacity"));
    setToolTip(accessibleName());
}

void QuickSlider::setTarget(KisDoubleSliderSpinBox *target)
{
    if (m_target) {
        disconnect(m_target, nullptr, this, nullptr);
    }
    m_target = target;
    if (target) {
        connect(target, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, QOverload<>::of(&QWidget::update));
    }
    setEnabled(target != nullptr);
    update();
}

KisDoubleSliderSpinBox *QuickSlider::target() const
{
    return m_target;
}

void QuickSlider::setLength(int length)
{
    m_length = length;
    updateGeometry();
}

QSize QuickSlider::sizeHint() const
{
    return QSize(dp(44), m_length);
}

void QuickSlider::paintEvent(QPaintEvent *)
{
    const Theme &t = Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal w = dp(m_dragging ? 36 : 30);
    const QRectF track((width() - w) / 2.0, dp(2), w, height() - dp(4));
    const qreal radius = w / 2.0;
    QColor bg = t.surface;
    bg.setAlphaF(0.92);
    painter.setPen(QPen(t.outline, 1.0));
    painter.setBrush(bg);
    painter.drawRoundedRect(track, radius, radius);

    if (m_target && isEnabled()) {
        const qreal f = fraction();
        const qreal fillHeight = qMax(w, track.height() * f);
        const QRectF fill(track.left(), track.bottom() - fillHeight, track.width(), fillHeight);
        QColor accent = t.accent;
        accent.setAlphaF(m_dragging ? 0.95 : 0.8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawRoundedRect(fill, radius, radius);
    }

    // Small glyph at the top hinting what the slider does.
    const QColor glyph = isEnabled() ? t.textDim : t.outline;
    const QPointF c(width() / 2.0, track.top() + radius);
    if (m_kind == Kind::Size) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(glyph);
        painter.drawEllipse(c, dp(5), dp(5));
    } else {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(glyph, 1.5));
        painter.drawEllipse(c, dp(5), dp(5));
        QColor half = glyph;
        half.setAlphaF(0.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(half);
        painter.drawEllipse(c, dp(3), dp(3));
    }
}

void QuickSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_target && isEnabled()) {
        m_dragging = true;
        m_dragStartY = eventPos(event).y();
        // The size scale is fixed for the duration of a drag, even if the
        // value goes beyond the soft maximum, so the slider doesn't jump.
        m_dragMaximum = 0.0;
        m_dragMaximum = sizeMaximum();
        m_dragStartFraction = fraction();
        Q_EMIT dragStateChanged(true, valueLabel());
        update();
    }
    event->accept();
}

void QuickSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        const int dy = eventPos(event).y() - m_dragStartY;
        const qreal len = qMax(1, height() - dp(8));
        setFraction(m_dragStartFraction - qreal(dy) / len);
        Q_EMIT dragStateChanged(true, valueLabel());
    }
    event->accept();
}

void QuickSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_dragging = false;
        m_dragMaximum = 0.0;
        Q_EMIT dragStateChanged(false, valueLabel());
        update();
    }
    event->accept();
}

qreal QuickSlider::sizeMaximum() const
{
    if (m_dragMaximum > 0.0) {
        return m_dragMaximum;
    }
    if (!m_target) {
        return 100.0;
    }
    // A soft maximum keeps the useful range fine-grained; it grows when the
    // brush is already larger, and never exceeds Krita's own maximum.
    const qreal soft = qMax(config::quickSizeMaximum(), m_target->value() * 1.25);
    return qMin(soft, m_target->maximum());
}

qreal QuickSlider::fraction() const
{
    if (!m_target) {
        return 0.0;
    }
    const qreal value = m_target->value();
    if (m_kind == Kind::Size) {
        // Logarithmic: equal finger movement changes the size by equal ratios.
        const qreal minimum = qMax(1.0, m_target->minimum());
        const qreal maximum = qMax(minimum * 1.01, sizeMaximum());
        const qreal v = qBound(minimum, value, maximum);
        return qLn(v / minimum) / qLn(maximum / minimum);
    }
    const qreal min = m_target->minimum();
    const qreal max = m_target->maximum();
    if (max <= min) {
        return 0.0;
    }
    return qBound(0.0, (value - min) / (max - min), 1.0);
}

void QuickSlider::setFraction(qreal f)
{
    if (!m_target) {
        return;
    }
    f = qBound(0.0, f, 1.0);
    qreal value;
    if (m_kind == Kind::Size) {
        const qreal minimum = qMax(1.0, m_target->minimum());
        const qreal maximum = qMax(minimum * 1.01, sizeMaximum());
        value = minimum * qExp(f * qLn(maximum / minimum));
        // Whole pixels above 10px, finer steps for tiny brushes.
        value = value >= 10.0 ? qRound(value) : qRound(value * 10.0) / 10.0;
    } else {
        value = qRound(m_target->minimum() + f * (m_target->maximum() - m_target->minimum()));
    }
    if (!qFuzzyCompare(value, m_target->value())) {
        m_target->setValue(value);
    }
    update();
}

QString QuickSlider::valueLabel() const
{
    if (!m_target) {
        return QString();
    }
    const QString number = QLocale().toString(m_target->value(), 'f', m_target->value() < 10.0 && m_kind == Kind::Size ? 1 : 0);
    return m_kind == Kind::Size ? i18nc("brush size in pixels", "Size %1 px", number)
                                : i18nc("opacity in percent", "Opacity %1%", number);
}

// ---------------------------------------------------------------------------
// QuickSliders

QuickSliders::QuickSliders(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mobileQuickSliders"));
    setProperty("mobileChrome", true);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(dp(6));

    m_size = new QuickSlider(QuickSlider::Kind::Size);
    connect(m_size, &QuickSlider::dragStateChanged, this, &QuickSliders::dragStateChanged);
    layout->addWidget(m_size, 0, Qt::AlignHCenter);

    m_picker = new ChromeButton(QStringLiteral("eyedropper"), i18n("Eyedropper"));
    m_picker->setButtonSize(44);
    m_picker->setFloating(true);
    m_picker->setCheckable(true);
    connect(m_picker, &ChromeButton::clicked, this, &QuickSliders::eyedropperClicked);
    connect(m_picker, &ChromeButton::longPressed, this, &QuickSliders::eyedropperLongPressed);
    layout->addWidget(m_picker, 0, Qt::AlignHCenter);

    m_opacity = new QuickSlider(QuickSlider::Kind::Opacity);
    connect(m_opacity, &QuickSlider::dragStateChanged, this, &QuickSliders::dragStateChanged);
    layout->addWidget(m_opacity, 0, Qt::AlignHCenter);
}

void QuickSliders::setTargets(KisDoubleSliderSpinBox *size, KisDoubleSliderSpinBox *opacity)
{
    m_size->setTarget(size);
    m_opacity->setTarget(opacity);
    m_size->setVisible(size != nullptr);
    m_opacity->setVisible(opacity != nullptr);
}

void QuickSliders::fitHeight(int height)
{
    const int pickerHeight = dp(44) + dp(12);
    const int length = qBound(dp(96), (height - pickerHeight) / 2, dp(200));
    m_size->setLength(length);
    m_opacity->setLength(length);
    adjustSize();
}

bool QuickSliders::hasTargets() const
{
    return m_size->target() || m_opacity->target();
}

void QuickSliders::setEyedropperChecked(bool checked)
{
    m_picker->setChecked(checked);
}

// ---------------------------------------------------------------------------
// ValueBubble

ValueBubble::ValueBubble(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    applyFont(this, TextRole::Body, true);
    hide();
}

void ValueBubble::setText(const QString &text)
{
    m_text = text;
    adjustSize();
    update();
}

QSize ValueBubble::sizeHint() const
{
    QFontMetrics fm(font());
    return QSize(fm.horizontalAdvance(m_text) + dp(28), dp(40));
}

void ValueBubble::paintEvent(QPaintEvent *)
{
    const Theme &t = Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(t.outline, 1.0));
    QColor bg = t.surface;
    bg.setAlphaF(0.95);
    painter.setBrush(bg);
    painter.drawRoundedRect(r, r.height() / 2.0, r.height() / 2.0);
    painter.setPen(t.text);
    painter.drawText(r, Qt::AlignCenter, m_text);
}

} // namespace mobileui
