/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Layout ported from the Drawpile Mobile fork (same author, GPL-3.0-or-later).
 */
#include "MobileHub.h"
#include "MobileTheme.h"
#include "MobileWidgets.h"

#include <KisDocument.h>
#include <KoDocumentInfo.h>
#include <KisMainWindow.h>
#include <KisPart.h>
#include <KisRecentFilesManager.h>
#include <KisView.h>
#include <KisViewManager.h>
#include <KoColor.h>
#include <KoColorSpaceRegistry.h>
#include <kis_action_manager.h>
#include <kis_action.h>
#include <kis_config.h>
#include <kis_group_layer.h>
#include <kis_image.h>
#include <kis_image_animation_interface.h>
#include <kis_keyframe_channel.h>
#include <kis_paint_layer.h>
#include <kis_time_span.h>
#include <kis_types.h>
#include <kritaversion.h>
#include <utils/KisFileIconCreator.h>

#include <klocalizedstring.h>

#include <QAction>
#include <QButtonGroup>
#include <QColorDialog>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace mobileui {

namespace {

QLabel *sectionLabel(const QString &text)
{
    QLabel *label = new QLabel(text);
    label->setProperty("mobileRole", QStringLiteral("title"));
    applyFont(label, TextRole::Subtitle, true);
    label->setContentsMargins(dp(4), dp(16), dp(4), dp(6));
    return label;
}

QScrollArea *makeScroll(QWidget *content)
{
    QScrollArea *scroll = new QScrollArea;
    scroll->setProperty("mobileChrome", true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    enableKineticScrolling(scroll);
    return scroll;
}

QString pathForUrl(const QUrl &url)
{
    // Krita keeps Android content:// URIs in local-file URLs.
    const QString local = url.toLocalFile();
    return local.isEmpty() ? url.toString() : local;
}

bool isLocalPath(const QString &path)
{
    return !path.startsWith(QStringLiteral("content:")) && QFileInfo(path).isFile();
}

void drawChecker(QPainter &painter, const QRectF &rect, const Theme &t)
{
    painter.save();
    painter.setClipRect(rect);
    const QColor a = t.dark ? QColor(0x3a, 0x3c, 0x42) : QColor(0xe8, 0xe9, 0xec);
    const QColor b = t.dark ? QColor(0x2e, 0x30, 0x35) : QColor(0xf6, 0xf6, 0xf8);
    painter.fillRect(rect, a);
    const int s = dp(8);
    for (int y = int(rect.top()); y < rect.bottom(); y += s) {
        for (int x = int(rect.left()); x < rect.right(); x += s) {
            if (((x - int(rect.left())) / s + (y - int(rect.top())) / s) % 2) {
                painter.fillRect(QRect(x, y, s, s), b);
            }
        }
    }
    painter.restore();
}

} // namespace

// Card in the recent files grid.
class RecentCard final : public QAbstractButton
{
public:
    explicit RecentCard(const QUrl &url, QWidget *parent = nullptr)
        : QAbstractButton(parent)
        , m_url(url)
    {
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        const QString path = pathForUrl(url);
        QString name = url.fileName();
        if (name.isEmpty()) {
            name = QFileInfo(path).fileName();
        }
        // content:// URIs often end in an encoded document id.
        name = QUrl::fromPercentEncoding(name.toUtf8()).section(QLatin1Char('/'), -1);
        m_name = QFileInfo(name).completeBaseName();
        if (m_name.isEmpty()) {
            m_name = name;
        }
        m_extension = QFileInfo(name).suffix().toUpper();
        setText(m_name);
        setAccessibleName(m_name);
        if (isLocalPath(path)) {
            const QFileInfo info(path);
            m_meta = QStringLiteral("%1 · %2")
                         .arg(QLocale().formattedDataSize(info.size(), 1, QLocale::DataSizeTraditionalFormat),
                              QLocale().toString(info.lastModified().date(), QLocale::ShortFormat));
        }
        m_longPress.setSingleShot(true);
        m_longPress.setInterval(450);
        QObject::connect(&m_longPress, &QTimer::timeout, this, [this] {
            if (isDown()) {
                m_longPressed = true;
                setDown(false);
                if (onMenu) {
                    onMenu();
                }
            }
        });
    }

    const QUrl &url() const { return m_url; }

    void setThumbnail(const QIcon &icon)
    {
        m_thumb = icon;
        update();
    }

    std::function<void()> onMenu;

    QSize sizeHint() const override
    {
        const int w = qMax(dp(150), width());
        return QSize(dp(160), thumbHeight(w) + dp(60));
    }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return thumbHeight(w) + dp(60); }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        m_longPressed = false;
        QAbstractButton::mousePressEvent(event);
        if (isDown()) {
            m_longPress.start();
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_longPress.stop();
        if (m_longPressed) {
            m_longPressed = false;
            setDown(false);
            return;
        }
        if (isDown() && menuRect().contains(event->pos())) {
            setDown(false);
            if (onMenu) {
                onMenu();
            }
            return;
        }
        QAbstractButton::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        const Theme &t = Theme::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = radiusMedium();
        QPainterPath clip;
        clip.addRoundedRect(r, radius, radius);
        painter.fillPath(clip, isDown() ? t.surface3 : t.surface2);

        const QRectF thumbRect(r.left(), r.top(), r.width(), thumbHeight(width()));
        painter.save();
        painter.setClipPath(clip);
        drawChecker(painter, thumbRect, t);
        if (!m_thumb.isNull()) {
            const QRect target = thumbRect.adjusted(dp(6), dp(6), -dp(6), -dp(6)).toRect();
            m_thumb.paint(&painter, target, Qt::AlignCenter);
        } else {
            QFont f = font();
            f.setPixelSize(dp(18));
            f.setBold(true);
            painter.setFont(f);
            painter.setPen(t.textDim);
            painter.drawText(thumbRect, Qt::AlignCenter, m_extension.isEmpty() ? QStringLiteral("?") : m_extension);
        }
        painter.restore();

        const QRectF m = menuRect();
        QColor bubble = t.surface;
        bubble.setAlphaF(0.85);
        painter.setPen(Qt::NoPen);
        painter.setBrush(bubble);
        painter.drawEllipse(m.adjusted(dp(6), dp(6), -dp(6), -dp(6)));
        mobileui::icon(QStringLiteral("more"), t.text).paint(&painter, m.adjusted(dp(12), dp(12), -dp(12), -dp(12)).toRect());

        QFont titleFont = font();
        titleFont.setPixelSize(fontPixelSize(TextRole::Label));
        titleFont.setBold(true);
        const QFontMetrics tfm(titleFont);
        QFont metaFont = font();
        metaFont.setPixelSize(fontPixelSize(TextRole::Caption));
        const QFontMetrics mfm(metaFont);
        const qreal x = r.left() + dp(12);
        const qreal w = r.width() - dp(24);
        const qreal y = thumbRect.bottom() + dp(10);
        painter.setFont(titleFont);
        painter.setPen(t.text);
        painter.drawText(QRectF(x, y, w, tfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                         tfm.elidedText(m_name, Qt::ElideMiddle, int(w)));
        painter.setFont(metaFont);
        painter.setPen(t.textDim);
        const QString meta = m_meta.isEmpty() ? m_extension : QStringLiteral("%1 · %2").arg(m_extension, m_meta);
        painter.drawText(QRectF(x, y + tfm.height() + dp(2), w, mfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                         mfm.elidedText(meta, Qt::ElideRight, int(w)));
    }

private:
    int thumbHeight(int w) const { return qRound(w * 0.72); }
    QRectF menuRect() const
    {
        const int s = dp(48);
        return QRectF(width() - s, 0, s, s);
    }

    QUrl m_url;
    QString m_name;
    QString m_extension;
    QString m_meta;
    QIcon m_thumb;
    bool m_longPressed = false;
    QTimer m_longPress;
};

// Simple modal list of choices sliding up from the bottom of the hub.
class ChoiceOverlay final : public QWidget
{
public:
    ChoiceOverlay(QWidget *parent,
                  const QString &title,
                  const QVector<QPair<QString, QString>> &choices,
                  const std::function<void(int)> &onChosen)
        : QWidget(parent)
        , m_onChosen(onChosen)
    {
        setObjectName(QStringLiteral("mobileChoiceOverlay"));
        setGeometry(parent->rect());
        QVBoxLayout *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->addStretch(1);
        QWidget *panel = new QWidget;
        panel->setObjectName(QStringLiteral("mobileChoicePanel"));
        panel->setAttribute(Qt::WA_StyledBackground, true);
        const Theme &t = Theme::current();
        panel->setStyleSheet(QStringLiteral("QWidget#mobileChoicePanel { background: %1; "
                                            "border-top-left-radius: %2px; border-top-right-radius: %2px; }")
                                 .arg(t.surface.name())
                                 .arg(radiusLarge()));
        panel->setMaximumWidth(dp(560));
        QVBoxLayout *layout = new QVBoxLayout(panel);
        layout->setContentsMargins(dp(16), dp(16), dp(16), dp(20));
        layout->setSpacing(dp(8));
        QLabel *label = new QLabel(title);
        label->setProperty("mobileRole", QStringLiteral("title"));
        label->setWordWrap(true);
        applyFont(label, TextRole::Subtitle, true);
        layout->addWidget(label);
        for (int i = 0; i < choices.size(); ++i) {
            ActionCard *card = new ActionCard(choices[i].first, choices[i].second);
            QObject::connect(card, &ActionCard::clicked, this, [this, i] {
                choose(i);
            });
            layout->addWidget(card);
        }
        QHBoxLayout *center = new QHBoxLayout;
        center->addWidget(panel);
        outer->addLayout(center);
        show();
        raise();
    }

    void choose(int i)
    {
        const std::function<void(int)> fn = m_onChosen;
        hide();
        deleteLater();
        if (fn && i >= 0) {
            fn(i);
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0, 120));
    }

    void mouseReleaseEvent(QMouseEvent *) override { choose(-1); }

private:
    std::function<void(int)> m_onChosen;
};

// ---------------------------------------------------------------------------

Hub::Hub(KisMainWindow *mainWindow, QWidget *parent)
    : QWidget(parent)
    , m_mainWindow(mainWindow)
{
    setObjectName(QStringLiteral("mobileHub"));
    setProperty("mobileChrome", true);
    setAttribute(Qt::WA_StyledBackground, true);
    hide();

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_pages = new QStackedWidget;
    layout->addWidget(m_pages);
    m_mainPage = buildMainPage();
    m_newPage = buildNewPage();
    m_pages->addWidget(m_mainPage);
    m_pages->addWidget(m_newPage);

    connect(KisRecentFilesManager::instance(), &KisRecentFilesManager::listRenewed, this, [this] {
        if (isVisible()) {
            refreshRecents();
        }
    });
    connect(KisRecentFilesManager::instance(), &KisRecentFilesManager::fileAdded, this, [this] {
        if (isVisible()) {
            refreshRecents();
        }
    });
    if (mainWindow) {
        connect(mainWindow, &KisMainWindow::activeViewChanged, this, [this] {
            if (m_expectLoad && m_mainWindow && m_mainWindow->activeView() && isVisible()) {
                m_expectLoad = false;
                Q_EMIT closeRequested();
            } else if (isVisible()) {
                refreshContinueCard();
            }
        });
    }
}

void Hub::createCanvasForTest()
{
    selectPreset(0);
    createCanvas();
}

bool Hub::isOnNewPage() const
{
    return m_pages->currentWidget() == m_newPage;
}

bool Hub::handleBack(bool release)
{
    for (QObject *child : children()) {
        if (ChoiceOverlay *overlay = dynamic_cast<ChoiceOverlay *>(child)) {
            if (overlay->isVisible()) {
                if (release) {
                    overlay->choose(-1);
                }
                return true;
            }
        }
    }
    if (m_pages->currentWidget() == m_newPage) {
        if (release) {
            showMainPage();
        }
        return true;
    }
    // Going back from the hub returns to the canvas if there is one.
    if (m_mainWindow && m_mainWindow->activeView()) {
        if (release) {
            Q_EMIT closeRequested();
        }
        return true;
    }
    return false;
}

void Hub::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    reflowGrids();
    for (QObject *child : children()) {
        if (ChoiceOverlay *overlay = dynamic_cast<ChoiceOverlay *>(child)) {
            overlay->setGeometry(rect());
        }
    }
}

void Hub::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshContinueCard();
    refreshRecents();
    refreshRecovery();
    reflowGrids();
}

void Hub::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Theme::current().background);
}

QWidget *Hub::buildMainPage()
{
    const Theme &t = Theme::current();
    QWidget *content = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(content);
    layout->setContentsMargins(dp(16), dp(8), dp(16), dp(24));
    layout->setSpacing(dp(8));

    // Header.
    QHBoxLayout *header = new QHBoxLayout;
    header->setSpacing(dp(4));
    QVBoxLayout *titles = new QVBoxLayout;
    titles->setSpacing(0);
    QLabel *title = new QLabel(QStringLiteral("Krita"));
    title->setProperty("mobileRole", QStringLiteral("title"));
    QFont titleFont = title->font();
    titleFont.setPixelSize(dp(26));
    titleFont.setBold(true);
    title->setFont(titleFont);
    titles->addWidget(title);
    QLabel *subtitle = new QLabel(i18n("Phone interface · unofficial fork · %1", QStringLiteral(KRITA_VERSION_STRING)));
    subtitle->setProperty("mobileRole", QStringLiteral("dim"));
    subtitle->setWordWrap(true);
    subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    applyFont(subtitle, TextRole::Caption);
    titles->addWidget(subtitle);
    header->addLayout(titles, 1);
    ChromeButton *settings = new ChromeButton(QStringLiteral("settings"), i18n("Configure Krita"));
    connect(settings, &ChromeButton::clicked, this, [this] {
        triggerAction(QStringLiteral("options_configure"));
    });
    header->addWidget(settings);
    ChromeButton *more = new ChromeButton(QStringLiteral("more"), i18n("More"));
    connect(more, &ChromeButton::clicked, this, [this] {
        showChoices(i18n("More"),
                    {{QStringLiteral("info"), i18n("About Krita")},
                     {QStringLiteral("info"), i18n("About this unofficial fork")},
                     {QStringLiteral("browse"), i18n("Krita manual")},
                     {QStringLiteral("back"), i18n("Quit")}},
                    [this](int i) {
                        switch (i) {
                        case 0:
                            triggerAction(QStringLiteral("help_about_app"));
                            break;
                        case 1:
                            showChoices(i18n("This is an unofficial, personal build of Krita with a phone interface. "
                                             "It is not made or supported by the Krita project. Krita is free software "
                                             "by the Krita developers and contributors, licensed under the GNU GPL."),
                                        {{QStringLiteral("check"), i18n("OK")}},
                                        [](int) {});
                            break;
                        case 2:
                            triggerAction(QStringLiteral("help_contents"));
                            break;
                        case 3:
                            triggerAction(QStringLiteral("file_quit"));
                            break;
                        default:
                            break;
                        }
                    });
    });
    header->addWidget(more);
    layout->addLayout(header);

    // Continue where you left off.
    m_continueCard = new QWidget;
    m_continueCard->setObjectName(QStringLiteral("mobileContinueCard"));
    m_continueCard->setAttribute(Qt::WA_StyledBackground, true);
    m_continueCard->setStyleSheet(QStringLiteral("QWidget#mobileContinueCard { background: %1; "
                                                 "border: 1px solid %2; border-radius: %3px; }")
                                      .arg(t.surface2.name(), t.outline.name())
                                      .arg(radiusMedium()));
    QHBoxLayout *continueLayout = new QHBoxLayout(m_continueCard);
    continueLayout->setContentsMargins(dp(12), dp(12), dp(12), dp(12));
    continueLayout->setSpacing(dp(12));
    m_continueThumb = new QLabel;
    m_continueThumb->setFixedSize(dp(84), dp(84));
    m_continueThumb->setAlignment(Qt::AlignCenter);
    continueLayout->addWidget(m_continueThumb);
    QVBoxLayout *continueText = new QVBoxLayout;
    continueText->setSpacing(dp(2));
    QLabel *continueHeading = new QLabel(i18n("Continue drawing"));
    continueHeading->setProperty("mobileRole", QStringLiteral("dim"));
    applyFont(continueHeading, TextRole::Caption, true);
    continueText->addWidget(continueHeading);
    m_continueTitle = new QLabel;
    m_continueTitle->setProperty("mobileRole", QStringLiteral("title"));
    m_continueTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    applyFont(m_continueTitle, TextRole::Subtitle, true);
    continueText->addWidget(m_continueTitle);
    m_continueMeta = new QLabel;
    m_continueMeta->setProperty("mobileRole", QStringLiteral("dim"));
    m_continueMeta->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    applyFont(m_continueMeta, TextRole::Caption);
    continueText->addWidget(m_continueMeta);
    continueText->addStretch(1);
    continueLayout->addLayout(continueText, 1);
    QPushButton *continueButton = new QPushButton(i18n("Open"));
    continueButton->setProperty("mobileRole", QStringLiteral("primary"));
    connect(continueButton, &QPushButton::clicked, this, &Hub::closeRequested);
    continueLayout->addWidget(continueButton, 0, Qt::AlignVCenter);
    layout->addWidget(m_continueCard);

    // Start actions.
    layout->addWidget(sectionLabel(i18n("Start")));
    QWidget *actions = new QWidget;
    m_actionGrid = new QGridLayout(actions);
    m_actionGrid->setContentsMargins(0, 0, 0, 0);
    m_actionGrid->setSpacing(dp(8));
    auto addAction = [this](const QString &iconName, const QString &label, const QString &sub, const std::function<void()> &fn) {
        ActionCard *card = new ActionCard(iconName, label, sub);
        connect(card, &ActionCard::clicked, this, fn);
        m_actionCards.append(card);
        return card;
    };
    addAction(QStringLiteral("plus"), i18n("New canvas"), i18n("Presets and animation"), [this] {
        showNewPage();
    });
    addAction(QStringLiteral("open"), i18n("Open file"), i18n("Krita, PSD, images…"), [this] {
        expectDocumentLoad();
        triggerAction(QStringLiteral("file_open"));
    });
    addAction(QStringLiteral("file"), i18n("All new document options"), i18n("Templates, color spaces, clipboard"), [this] {
        expectDocumentLoad();
        triggerAction(QStringLiteral("file_new"));
    });
    m_recoverCard = addAction(QStringLiteral("recover"), i18n("Recover"), i18n("Autosaved work"), [this] {
        const QStringList files = autosaveFiles();
        if (files.isEmpty()) {
            showChoices(i18n("There is no autosaved work to recover."), {{QStringLiteral("check"), i18n("OK")}}, [](int) {});
            return;
        }
        QVector<QPair<QString, QString>> choices;
        for (const QString &file : files) {
            const QFileInfo info(QDir(autosaveLocation()).filePath(file));
            choices.append({QStringLiteral("recover"),
                            i18n("Autosave from %1", QLocale().toString(info.lastModified(), QLocale::ShortFormat))});
        }
        showChoices(i18n("Open autosaved work. Save it under a new name afterwards."), choices, [this, files](int i) {
            if (i < 0 || i >= files.size() || !m_mainWindow) {
                return;
            }
            expectDocumentLoad();
            m_mainWindow->openDocument(QDir(autosaveLocation()).filePath(files[i]), KisMainWindow::RecoveryFile);
        });
    });
    layout->addWidget(actions);

    // Recent files.
    layout->addWidget(sectionLabel(i18n("Recent")));
    QWidget *recent = new QWidget;
    m_recentGrid = new QGridLayout(recent);
    m_recentGrid->setContentsMargins(0, 0, 0, 0);
    m_recentGrid->setSpacing(dp(10));
    layout->addWidget(recent);
    m_recentEmpty = new QLabel(i18n("Files you open or save show up here."));
    m_recentEmpty->setProperty("mobileRole", QStringLiteral("dim"));
    m_recentEmpty->setWordWrap(true);
    applyFont(m_recentEmpty, TextRole::Body);
    m_recentEmpty->setContentsMargins(dp(4), dp(8), dp(4), dp(8));
    layout->addWidget(m_recentEmpty);
    layout->addStretch(1);

    return makeScroll(content);
}

QWidget *Hub::buildNewPage()
{
    QWidget *page = new QWidget;
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    QHBoxLayout *header = new QHBoxLayout;
    header->setContentsMargins(dp(8), dp(4), dp(16), dp(4));
    ChromeButton *back = new ChromeButton(QStringLiteral("back"), i18n("Back"));
    connect(back, &ChromeButton::clicked, this, &Hub::showMainPage);
    header->addWidget(back);
    QLabel *title = new QLabel(i18n("New canvas"));
    title->setProperty("mobileRole", QStringLiteral("title"));
    applyFont(title, TextRole::Title, true);
    header->addWidget(title, 1);
    pageLayout->addLayout(header);

    QWidget *content = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(content);
    layout->setContentsMargins(dp(16), 0, dp(16), dp(24));
    layout->setSpacing(dp(8));

    const QSize portrait = phoneSize(true);
    m_presets = {
        {QStringLiteral("portrait"), i18n("Phone portrait"), portrait, 300, false},
        {QStringLiteral("landscape"), i18n("Phone landscape"), portrait.transposed(), 300, false},
        {QStringLiteral("square"), i18n("Square"), QSize(2048, 2048), 300, false},
        {QStringLiteral("a4"), i18n("A4, 300 dpi"), QSize(2480, 3508), 300, false},
        {QStringLiteral("film"), i18n("Animation 1080p"), QSize(1920, 1080), 72, true},
        {QStringLiteral("film"), i18n("Animation 720p"), QSize(1280, 720), 72, true},
    };

    layout->addWidget(sectionLabel(i18n("Presets")));
    QWidget *presets = new QWidget;
    m_presetGrid = new QGridLayout(presets);
    m_presetGrid->setContentsMargins(0, 0, 0, 0);
    m_presetGrid->setSpacing(dp(8));
    for (int i = 0; i < m_presets.size(); ++i) {
        const Preset &p = m_presets[i];
        ActionCard *card = new ActionCard(p.iconName, p.title, QStringLiteral("%1 × %2").arg(p.size.width()).arg(p.size.height()));
        card->setCheckable(true);
        connect(card, &ActionCard::clicked, this, [this, i] {
            selectPreset(i);
        });
        m_presetCards.append(card);
    }
    layout->addWidget(presets);

    m_fpsRow = new QWidget;
    QVBoxLayout *fpsLayout = new QVBoxLayout(m_fpsRow);
    fpsLayout->setContentsMargins(0, 0, 0, 0);
    fpsLayout->addWidget(sectionLabel(i18n("Frame rate")));
    QWidget *fpsChipsWidget = new QWidget;
    FlowLayout *fpsChips = new FlowLayout(fpsChipsWidget);
    m_fpsGroup = new QButtonGroup(this);
    for (int fps : {8, 12, 15, 24, 25, 30, 60}) {
        QToolButton *chip = new QToolButton;
        chip->setProperty("mobileChip", true);
        chip->setCheckable(true);
        chip->setText(i18nc("frames per second", "%1 fps", fps));
        applyFont(chip, TextRole::Label, true);
        m_fpsGroup->addButton(chip, fps);
        fpsChips->addWidget(chip);
        if (fps == 24) {
            chip->setChecked(true);
        }
    }
    fpsLayout->addWidget(fpsChipsWidget);
    QLabel *fpsNote = new QLabel(i18n("The frame rate and the frame range can be changed later in the animation panel. "
                                      "The first layer gets a key frame, so drawing goes into the animation right away."));
    fpsNote->setWordWrap(true);
    fpsNote->setProperty("mobileRole", QStringLiteral("dim"));
    applyFont(fpsNote, TextRole::Caption);
    fpsLayout->addWidget(fpsNote);
    m_fpsRow->hide();
    layout->addWidget(m_fpsRow);

    layout->addWidget(sectionLabel(i18n("Size in pixels")));
    QHBoxLayout *sizeRow = new QHBoxLayout;
    sizeRow->setSpacing(dp(8));
    auto makeSpin = [](int min, int max) {
        QSpinBox *box = new QSpinBox;
        box->setProperty("mobileChrome", true);
        box->setRange(min, max);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
        box->setAlignment(Qt::AlignCenter);
        applyFont(box, TextRole::Body, true);
        return box;
    };
    m_widthBox = makeSpin(1, 100000);
    m_widthBox->setAccessibleName(i18n("Width"));
    m_heightBox = makeSpin(1, 100000);
    m_heightBox->setAccessibleName(i18n("Height"));
    m_widthBox->setValue(portrait.width());
    m_heightBox->setValue(portrait.height());
    sizeRow->addWidget(m_widthBox, 1);
    QLabel *times = new QLabel(QStringLiteral("×"));
    applyFont(times, TextRole::Subtitle, true);
    sizeRow->addWidget(times);
    sizeRow->addWidget(m_heightBox, 1);
    ChromeButton *swap = new ChromeButton(QStringLiteral("swap"), i18n("Swap width and height"));
    connect(swap, &ChromeButton::clicked, this, [this] {
        const int w = m_widthBox->value();
        m_widthBox->setValue(m_heightBox->value());
        m_heightBox->setValue(w);
    });
    sizeRow->addWidget(swap);
    layout->addLayout(sizeRow);

    layout->addWidget(sectionLabel(i18n("Resolution")));
    QHBoxLayout *dpiRow = new QHBoxLayout;
    m_dpiBox = makeSpin(1, 10000);
    m_dpiBox->setSuffix(i18nc("pixels per inch", " ppi"));
    m_dpiBox->setValue(300);
    dpiRow->addWidget(m_dpiBox, 1);
    dpiRow->addStretch(2);
    layout->addLayout(dpiRow);

    auto sizeEdited = [this] {
        if (m_selectedPreset >= 0) {
            const Preset &p = m_presets[m_selectedPreset];
            const QSize size(m_widthBox->value(), m_heightBox->value());
            if (p.size != size && p.size.transposed() != size) {
                m_presetCards[m_selectedPreset]->setChecked(false);
                m_selectedPreset = -1;
                m_fpsRow->hide();
            }
        }
        updateNewPageSummary();
    };
    connect(m_widthBox, QOverload<int>::of(&QSpinBox::valueChanged), this, sizeEdited);
    connect(m_heightBox, QOverload<int>::of(&QSpinBox::valueChanged), this, sizeEdited);
    connect(m_dpiBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &Hub::updateNewPageSummary);

    layout->addWidget(sectionLabel(i18n("Background")));
    QWidget *bgRowWidget = new QWidget;
    FlowLayout *bgRow = new FlowLayout(bgRowWidget);
    m_backgroundGroup = new QButtonGroup(this);
    const QStringList bgNames = {i18n("White"), i18n("Transparent"), i18n("Black"), i18n("Custom…")};
    for (int i = 0; i < bgNames.size(); ++i) {
        QToolButton *chip = new QToolButton;
        chip->setProperty("mobileChip", true);
        chip->setCheckable(true);
        chip->setText(bgNames[i]);
        applyFont(chip, TextRole::Label, true);
        m_backgroundGroup->addButton(chip, i);
        bgRow->addWidget(chip);
    }
    m_backgroundGroup->button(0)->setChecked(true);
    connect(m_backgroundGroup, &QButtonGroup::idClicked, this, [this](int id) {
        if (id == 3) {
            const QColor color = QColorDialog::getColor(m_customBackground, this, i18n("Background color"));
            if (color.isValid()) {
                m_customBackground = color;
            }
        }
        updateNewPageSummary();
    });
    layout->addWidget(bgRowWidget);

    m_summary = new QLabel;
    m_summary->setWordWrap(true);
    m_summary->setProperty("mobileRole", QStringLiteral("dim"));
    applyFont(m_summary, TextRole::Label);
    m_summary->setContentsMargins(dp(4), dp(12), dp(4), dp(4));
    layout->addWidget(m_summary);

    QPushButton *create = new QPushButton(i18n("Create canvas"));
    create->setProperty("mobileRole", QStringLiteral("primary"));
    connect(create, &QPushButton::clicked, this, &Hub::createCanvas);
    layout->addWidget(create);
    QPushButton *full = new QPushButton(i18n("More options…"));
    full->setProperty("mobileRole", QStringLiteral("secondary"));
    connect(full, &QPushButton::clicked, this, [this] {
        expectDocumentLoad();
        triggerAction(QStringLiteral("file_new"));
    });
    layout->addWidget(full);
    layout->addStretch(1);

    pageLayout->addWidget(makeScroll(content), 1);
    updateNewPageSummary();
    return page;
}

void Hub::refreshRecents()
{
    for (RecentCard *card : m_recentCards) {
        m_recentGrid->removeWidget(card);
        card->deleteLater();
    }
    m_recentCards.clear();
    const QList<QUrl> urls = KisRecentFilesManager::instance()->recentUrlsLatestFirst();
    const qreal dpr = devicePixelRatioF();
    const QSize iconSize(dp(200), dp(150));
    for (const QUrl &url : urls) {
        RecentCard *card = new RecentCard(url);
        connect(card, &RecentCard::clicked, this, [this, url] {
            openUrl(url);
        });
        card->onMenu = [this, url] {
            showRecentActions(url);
        };
        m_recentCards.append(card);

        // Thumbnails are read in the background, the same way Krita's welcome
        // page does it.
        const QString path = pathForUrl(url);
        QPointer<RecentCard> guarded = card;
        auto *watcher = new QFutureWatcher<QIcon>(this);
        connect(watcher, &QFutureWatcher<QIcon>::finished, this, [watcher, guarded] {
            if (guarded) {
                guarded->setThumbnail(watcher->result());
            }
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run([path, dpr, iconSize] {
            QIcon icon;
            KisFileIconCreator creator;
            if (!creator.createFileIcon(path, icon, dpr, iconSize)) {
                icon = QIcon();
            }
            return icon;
        }));
    }
    m_recentEmpty->setVisible(m_recentCards.isEmpty());
    m_columns = 0;
    reflowGrids();
}

void Hub::refreshContinueCard()
{
    KisView *view = m_mainWindow ? m_mainWindow->activeView() : nullptr;
    KisDocument *doc = view ? view->document() : nullptr;
    m_continueCard->setVisible(doc != nullptr);
    if (!doc) {
        return;
    }
    const qreal dpr = devicePixelRatioF();
    const int s = qRound(dp(84) * dpr);
    QPixmap pixmap(s, s);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(0, 0, s, s), s / 8.0, s / 8.0);
        painter.setClipPath(clip);
        drawChecker(painter, QRectF(0, 0, s, s), Theme::current());
        const QPixmap preview = doc->generatePreview(QSize(s, s));
        if (!preview.isNull()) {
            const QSizeF target = QSizeF(preview.size()).scaled(s, s, Qt::KeepAspectRatio);
            painter.drawPixmap(QRectF((s - target.width()) / 2.0, (s - target.height()) / 2.0, target.width(), target.height()),
                               preview,
                               QRectF(preview.rect()));
        }
    }
    pixmap.setDevicePixelRatio(dpr);
    m_continueThumb->setPixmap(pixmap);
    QString title;
    if (!doc->path().isEmpty()) {
        title = QFileInfo(doc->path()).completeBaseName();
    }
    if (title.isEmpty() && doc->documentInfo()) {
        title = doc->documentInfo()->aboutInfo(QStringLiteral("title"));
    }
    if (title.isEmpty()) {
        title = i18n("Untitled");
    }
    m_continueTitle->setText(title);
    QStringList parts;
    if (KisImageSP image = doc->image()) {
        parts.append(QStringLiteral("%1×%2").arg(image->width()).arg(image->height()));
        KisImageAnimationInterface *animation = image->animationInterface();
        if (animation && animation->hasAnimation()) {
            parts.append(i18nc("frames per second", "%1 fps", animation->framerate()));
        }
    }
    if (doc->isModified()) {
        parts.append(i18n("unsaved changes"));
    }
    m_continueMeta->setText(parts.join(QStringLiteral(" · ")));
}

QString Hub::autosaveLocation() const
{
    // Same as KisAutoSaveRecoveryDialog::autoSaveLocation(), which is not
    // exported from Krita's UI library.
#if defined(Q_OS_WIN)
    return QDir::tempPath();
#elif defined(Q_OS_ANDROID)
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).append(QStringLiteral("/krita-backup"));
#else
    return QDir::homePath();
#endif
}

QStringList Hub::autosaveFiles() const
{
    const QDir dir(autosaveLocation());
    QStringList files = dir.entryList({QStringLiteral(".krita-*-*-autosave.kra")}, QDir::Files | QDir::Hidden, QDir::Time);
    files += dir.entryList({QStringLiteral("krita-*-*-autosave.kra")}, QDir::Files, QDir::Time);
    return files;
}

void Hub::refreshRecovery()
{
    const int count = autosaveFiles().size();
    m_recoverCard->setBadge(count > 0 ? QString::number(count) : QString());
    m_recoverCard->setSubtitle(count > 0 ? i18np("%1 autosave can be recovered", "%1 autosaves can be recovered", count)
                                         : i18n("Autosaved work"));
}

void Hub::reflowGrids()
{
    const int available = width() - dp(32);
    const int actionColumns = qBound(2, available / dp(170), 4);
    if (actionColumns != m_actionColumns) {
        m_actionColumns = actionColumns;
        for (ActionCard *card : m_actionCards) {
            m_actionGrid->removeWidget(card);
        }
        for (int i = 0; i < m_actionCards.size(); ++i) {
            m_actionGrid->addWidget(m_actionCards[i], i / actionColumns, i % actionColumns);
        }
    }

    const int columns = qBound(2, available / dp(170), 6);
    if (columns != m_columns) {
        m_columns = columns;
        for (RecentCard *card : m_recentCards) {
            m_recentGrid->removeWidget(card);
        }
        for (int i = 0; i < m_recentCards.size(); ++i) {
            m_recentGrid->addWidget(m_recentCards[i], i / columns, i % columns);
        }
        for (int c = 0; c < 6; ++c) {
            m_recentGrid->setColumnStretch(c, c < columns ? 1 : 0);
        }
    }

    const int presetColumns = qBound(1, available / dp(220), 3);
    if (presetColumns != m_presetColumns) {
        m_presetColumns = presetColumns;
        for (ActionCard *card : m_presetCards) {
            m_presetGrid->removeWidget(card);
        }
        for (int i = 0; i < m_presetCards.size(); ++i) {
            m_presetGrid->addWidget(m_presetCards[i], i / presetColumns, i % presetColumns);
        }
    }
}

void Hub::showRecentActions(const QUrl &url)
{
    showChoices(QFileInfo(url.fileName()).fileName(),
                {{QStringLiteral("open"), i18n("Open")}, {QStringLiteral("close"), i18n("Remove from list")}},
                [this, url](int i) {
                    if (i == 0) {
                        openUrl(url);
                    } else if (i == 1) {
                        KisRecentFilesManager::instance()->remove(url);
                        refreshRecents();
                    }
                });
}

void Hub::showChoices(const QString &title, const QVector<QPair<QString, QString>> &choices, const std::function<void(int)> &onChosen)
{
    new ChoiceOverlay(this, title, choices, onChosen);
}

void Hub::openUrl(const QUrl &url)
{
    if (!m_mainWindow) {
        return;
    }
    expectDocumentLoad();
    m_mainWindow->openDocument(pathForUrl(url), KisMainWindow::None);
}

void Hub::createCanvas()
{
    if (!m_mainWindow) {
        return;
    }
    const QSize size(m_widthBox->value(), m_heightBox->value());
    QColor background;
    switch (m_backgroundGroup->checkedId()) {
    case 1:
        background = QColor(255, 255, 255, 0);
        break;
    case 2:
        background = Qt::black;
        break;
    case 3:
        background = m_customBackground;
        break;
    default:
        background = Qt::white;
        break;
    }
    int fps = 0;
    if (m_selectedPreset >= 0 && m_presets[m_selectedPreset].animation) {
        fps = m_fpsGroup->checkedId();
    }

    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisDocument *doc = KisPart::instance()->createDocument();
    // Krita's resolution is in pixels per point.
    const double resolution = m_dpiBox->value() / 72.0;
    const QString name = i18n("Unnamed");
    if (!doc->newImage(name, size.width(), size.height(), cs, KoColor(background, cs), KisConfig::RASTER_LAYER, 2, QString(), resolution)) {
        delete doc;
        return;
    }
    if (fps > 0 && doc->image()) {
        KisImageSP image = doc->image();
        KisImageAnimationInterface *animation = image->animationInterface();
        animation->setFramerate(fps);
        animation->setDocumentRange(KisTimeSpan::fromTimeToTime(0, fps * 4 - 1));
        // Give the top paint layer a key frame on frame 0, so the first
        // drawing ends up in the animation right away.
        if (KisNodeSP top = image->rootLayer()->lastChild()) {
            if (KisPaintLayer *layer = dynamic_cast<KisPaintLayer *>(top.data())) {
                layer->enableAnimation();
                if (KisKeyframeChannel *channel = layer->getKeyframeChannel(KisKeyframeChannel::Raster.id(), true)) {
                    channel->addKeyframe(0);
                }
            }
        }
    }
    doc->setModified(false);
    expectDocumentLoad();
    KisPart::instance()->addDocument(doc);
    m_mainWindow->addViewAndNotifyLoadingCompleted(doc);
    if (fps > 0) {
        setProperty("openAnimationAfterCreate", true);
    }
}

void Hub::selectPreset(int index)
{
    m_selectedPreset = index;
    for (int i = 0; i < m_presetCards.size(); ++i) {
        m_presetCards[i]->setChecked(i == index);
    }
    const Preset &p = m_presets[index];
    {
        QSignalBlocker b1(m_widthBox);
        QSignalBlocker b2(m_heightBox);
        m_widthBox->setValue(p.size.width());
        m_heightBox->setValue(p.size.height());
    }
    m_dpiBox->setValue(p.dpi);
    m_fpsRow->setVisible(p.animation);
    updateNewPageSummary();
}

void Hub::updateNewPageSummary()
{
    const qint64 pixels = qint64(m_widthBox->value()) * m_heightBox->value();
    const double inchesW = m_widthBox->value() / double(qMax(1, m_dpiBox->value()));
    const double inchesH = m_heightBox->value() / double(qMax(1, m_dpiBox->value()));
    QString text = i18n("%1 × %2 pixels, %3 × %4 cm at %5 ppi",
                        m_widthBox->value(),
                        m_heightBox->value(),
                        QLocale().toString(inchesW * 2.54, 'f', 1),
                        QLocale().toString(inchesH * 2.54, 'f', 1),
                        m_dpiBox->value());
    if (pixels > 64LL * 1024 * 1024) {
        text += QStringLiteral("\n") + i18n("This is a very large canvas and may be slow or run out of memory on a phone.");
    }
    m_summary->setText(text);
}

void Hub::triggerAction(const QString &name)
{
    if (!m_mainWindow) {
        return;
    }
    if (QAction *action = m_mainWindow->viewManager()->actionManager()->actionByName(name)) {
        action->trigger();
    } else if (QAction *action = m_mainWindow->findChild<QAction *>(name)) {
        action->trigger();
    }
}

void Hub::expectDocumentLoad()
{
    m_expectLoad = true;
}

void Hub::showMainPage()
{
    m_pages->setCurrentWidget(m_mainPage);
}

void Hub::showNewPage()
{
    m_pages->setCurrentWidget(m_newPage);
    reflowGrids();
}

QSize Hub::phoneSize(bool portrait) const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    QSize size(1080, 2400);
    if (screen) {
        const QSize s(int(screen->size().width() * screen->devicePixelRatio()),
                      int(screen->size().height() * screen->devicePixelRatio()));
        if (s.width() > 0 && s.height() > 0) {
            size = s;
        }
    }
    if ((size.width() > size.height()) == portrait) {
        size.transpose();
    }
    return size;
}

} // namespace mobileui
