/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "MobileTestDriver.h"
#include "MobileCommandBrowser.h"
#include "MobileHub.h"
#include "MobilePerf.h"
#include "MobileSheet.h"
#include "MobileShell.h"

#include <KisDocument.h>
#include <KisMainWindow.h>
#include <KisPart.h>
#include <KoColor.h>
#include <KoColorSpaceRegistry.h>
#include <kis_config.h>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QTextStream>
#include <QTimer>

#include <functional>

namespace mobileui {

namespace {

class TestDriver final : public QObject
{
public:
    TestDriver(Shell *shell, const QString &outDir)
        : QObject(shell)
        , m_shell(shell)
        , m_dir(outDir)
    {
        QDir().mkpath(m_dir);
        m_log.setFileName(QDir(m_dir).filePath(QStringLiteral("testdriver.log")));
        m_log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
        if (shell->isActive()) {
            buildSteps();
        } else {
            buildClassicSteps();
        }
        QTimer::singleShot(6000, this, [this] {
            next();
        });
    }

private:
    using Step = std::function<void()>;

    void log(const QString &message)
    {
        QTextStream(&m_log) << message << '\n';
        m_log.flush();
    }

    KisMainWindow *mw() const { return m_shell ? m_shell->mainWindow() : nullptr; }

    void shot(const QString &name)
    {
        if (!mw()) {
            return;
        }
        const QPixmap pixmap = mw()->grab();
        const QString path = QDir(m_dir).filePath(QStringLiteral("%1-%2.png").arg(m_orientation, name));
        log(QStringLiteral("screenshot %1 %2").arg(path, pixmap.save(path) ? QStringLiteral("ok") : QStringLiteral("FAILED")));
    }

    void setSize(int w, int h, const QString &orientation)
    {
        m_orientation = orientation;
        if (mw()) {
            mw()->setMinimumSize(0, 0);
            mw()->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            mw()->resize(w, h);
        }
    }

    void openPanelAndShoot(const QString &panelId, const QString &tabId, const QString &name)
    {
        m_steps.append([this, panelId, tabId] {
            if (m_shell) {
                m_shell->hideHub();
                m_shell->openPanel(panelId, tabId);
            }
        });
        m_steps.append([this, name] {
            shot(name);
        });
    }

    void addScreens()
    {
        m_steps.append([this] {
            if (m_shell) {
                m_shell->closePanel();
                m_shell->showHub();
            }
        });
        m_steps.append([this] { shot(QStringLiteral("01-hub")); });
        m_steps.append([this] {
            if (m_shell && m_shell->hub()) {
                m_shell->hub()->showNewPage();
            }
        });
        m_steps.append([this] { shot(QStringLiteral("02-new-canvas")); });
        m_steps.append([this] {
            if (m_shell && m_shell->hub()) {
                m_shell->hub()->showMainPage();
                // Create a canvas through the same path as the button.
                if (!mw()->activeView()) {
                    m_shell->hub()->createCanvasForTest();
                }
            }
        });
        m_steps.append([this] {
            if (m_shell) {
                m_shell->hideHub();
                m_shell->closePanel();
            }
        });
        m_steps.append([this] {
            shot(QStringLiteral("03-editor"));
            // Which widgets cover the bottom-left corner above the rail.
            if (mw()) {
                const QPoint probe(mw()->width() > mw()->height() ? 70 : 20, mw()->height() - 50);
                for (QWidget *w : mw()->findChildren<QWidget *>()) {
                    if (w->isVisible() && QRect(w->mapTo(mw(), QPoint(0, 0)), w->size()).contains(probe)) {
                        const QRect g(w->mapTo(mw(), QPoint(0, 0)), w->size());
                        log(QStringLiteral("probe %1,%2: %3 '%4' %5,%6 %7x%8")
                                .arg(probe.x())
                                .arg(probe.y())
                                .arg(QString::fromLatin1(w->metaObject()->className()), w->objectName())
                                .arg(g.x())
                                .arg(g.y())
                                .arg(g.width())
                                .arg(g.height()));
                    }
                }
            }
        });
        openPanelAndShoot(QStringLiteral("tools"), QString(), QStringLiteral("04-tools"));
        openPanelAndShoot(QStringLiteral("brush"), QStringLiteral("presets"), QStringLiteral("05-brush-presets"));
        openPanelAndShoot(QStringLiteral("brush"), QStringLiteral("tool"), QStringLiteral("06-tool-options"));
        openPanelAndShoot(QStringLiteral("brush"), QStringLiteral("editor"), QStringLiteral("07-brush-editor"));
        openPanelAndShoot(QStringLiteral("color"), QStringLiteral("advanced"), QStringLiteral("08-color-advanced"));
        openPanelAndShoot(QStringLiteral("color"), QStringLiteral("widegamut"), QStringLiteral("09-color-widegamut"));
        openPanelAndShoot(QStringLiteral("color"), QStringLiteral("palette"), QStringLiteral("10-color-palette"));
        openPanelAndShoot(QStringLiteral("layers"), QStringLiteral("layers"), QStringLiteral("11-layers"));
        openPanelAndShoot(QStringLiteral("animation"), QStringLiteral("timeline"), QStringLiteral("12-timeline"));
        openPanelAndShoot(QStringLiteral("animation"), QStringLiteral("onion"), QStringLiteral("13-onion-skins"));
        openPanelAndShoot(QStringLiteral("animation"), QStringLiteral("curves"), QStringLiteral("14-curves"));
        openPanelAndShoot(QStringLiteral("view"), QStringLiteral("overview"), QStringLiteral("15-overview"));
        openPanelAndShoot(QStringLiteral("view"), QStringLiteral("history"), QStringLiteral("16-history"));
        openPanelAndShoot(QStringLiteral("text"), QStringLiteral("text"), QStringLiteral("17-text"));
        openPanelAndShoot(QStringLiteral("menu"), QString(), QStringLiteral("18-menu"));
        m_steps.append([this] {
            if (m_shell && m_shell->commandBrowser()) {
                m_shell->openPanel(QStringLiteral("menu"));
                m_shell->commandBrowser()->showAllCommands();
            }
        });
        m_steps.append([this] { shot(QStringLiteral("18b-all-commands")); });
        openPanelAndShoot(QStringLiteral("more"), QString(), QStringLiteral("19-more"));
        m_steps.append([this] {
            if (m_shell) {
                m_shell->closePanel();
            }
        });
    }

    // Opens one of Krita's dialogs through its action, screenshots it and
    // closes it again. Modal dialogs run a nested event loop, so the grab is
    // scheduled before triggering; the window that appeared after the trigger
    // is the one grabbed, and the next step waits until it is closed.
    void addDialog(const QString &actionName)
    {
        m_steps.append([this, actionName] {
            if (!m_shell) {
                return;
            }
            m_shell->closePanel();
            QAction *action = m_shell->findAction(actionName);
            if (!action) {
                log(QStringLiteral("dialog %1: action not found").arg(actionName));
                return;
            }
            if (!action->isEnabled()) {
                log(QStringLiteral("dialog %1: action disabled").arg(actionName));
                return;
            }
            QSet<QWidget *> before;
            for (QWidget *w : QApplication::topLevelWidgets()) {
                if (w->isVisible()) {
                    before.insert(w);
                }
            }
            m_extraDelay = 2500;
            QTimer::singleShot(2500, this, [this, actionName, before] {
                grabDialog(actionName, before);
            });
            log(QStringLiteral("dialog %1: triggering").arg(actionName));
            action->trigger();
        });
    }

    void grabDialog(const QString &actionName, const QSet<QWidget *> &before)
    {
        QWidget *found = nullptr;
        QWidget *modal = QApplication::activeModalWidget();
        if (modal && !before.contains(modal)) {
            found = modal;
        }
        if (!found) {
            for (QWidget *w : QApplication::topLevelWidgets()) {
                if (w->isVisible() && !before.contains(w) && w != mw() && !(w->windowFlags() & Qt::ToolTip)
                    && (w->windowType() == Qt::Dialog || w->windowType() == Qt::Window || qobject_cast<QDialog *>(w))) {
                    found = w;
                    if (qobject_cast<QDialog *>(w)) {
                        break;
                    }
                }
            }
        }
        if (!found) {
            log(QStringLiteral("dialog %1: no window appeared").arg(actionName));
            return;
        }
        QString safe = actionName;
        safe.replace(QLatin1Char(' '), QLatin1Char('_'));
        const QString path = QDir(m_dir).filePath(QStringLiteral("dialog-%1-%2.png").arg(m_orientation, safe));
        const bool ok = found->grab().save(path);
        log(QStringLiteral("dialog %1: %2 %3x%4 min %5x%6 %7")
                .arg(actionName, QString::fromLatin1(found->metaObject()->className()))
                .arg(found->width())
                .arg(found->height())
                .arg(found->minimumSizeHint().width())
                .arg(found->minimumSizeHint().height())
                .arg(ok ? QStringLiteral("saved") : QStringLiteral("FAILED")));
        QPointer<QWidget> guard(found);
        // Close from a fresh event loop iteration, not from inside whatever
        // the dialog is currently doing.
        QTimer::singleShot(0, this, [this, guard, actionName] {
            if (!guard) {
                return;
            }
            if (QDialog *dialog = qobject_cast<QDialog *>(guard.data())) {
                dialog->reject();
            } else {
                guard->close();
            }
            log(QStringLiteral("dialog %1: closed").arg(actionName));
        });
    }

    void addDialogs()
    {
        for (const char *name : {"file_new", "options_configure", "imagesize", "canvassize", "image_properties",
                                 "layer_properties", "layer_style", "krita_filter_gaussian blur",
                                 "krita_filter_hsvadjustment", "krita_filter_perchannel", "krita_filter_levels",
                                 "rotateimage", "offsetimage", "imagesplit", "layersplit", "separate",
                                 "clones_array", "render_animation"}) {
            addDialog(QString::fromLatin1(name));
        }
    }

    void logPerf(const QString &when)
    {
        log(QStringLiteral("perf %1: interface-ready-ms=%2 first-canvas-ms=%3 rss-kb=%4 peak-kb=%5 uptime-ms=%6")
                .arg(when)
                .arg(perf::interfaceReadyMs())
                .arg(perf::firstCanvasMs())
                .arg(perf::residentKb())
                .arg(perf::peakResidentKb())
                .arg(perf::processUptimeMs()));
    }

    // Times opening each panel (the synchronous part: building, layout).
    void addPanelTimings()
    {
        m_steps.append([this] {
            if (!m_shell) {
                return;
            }
            m_shell->hideHub();
            for (const QString &panelId : m_shell->panelIds()) {
                QElapsedTimer timer;
                timer.start();
                m_shell->openPanel(panelId);
                QApplication::processEvents();
                log(QStringLiteral("perf panel %1: %2 ms").arg(panelId).arg(timer.elapsed()));
                m_shell->closePanel();
                QApplication::processEvents();
            }
        });
    }

    // Baseline: Krita's own interface at phone size, for the before/after
    // comparison (screenshots, startup time, memory).
    void buildClassicSteps()
    {
        m_steps.append([this] { logPerf(QStringLiteral("classic-start")); });
        m_steps.append([this] { setSize(411, 891, QStringLiteral("classic-portrait")); });
        m_steps.append([this] { shot(QStringLiteral("01-start")); });
        m_steps.append([this] {
            if (!mw()) {
                return;
            }
            const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
            KisDocument *doc = KisPart::instance()->createDocument();
            if (!doc->newImage(QStringLiteral("Baseline"), 1080, 1920, cs, KoColor(Qt::white, cs), KisConfig::RASTER_LAYER, 2,
                               QString(), 300.0 / 72.0)) {
                delete doc;
                log(QStringLiteral("classic: canvas creation failed"));
                return;
            }
            doc->setModified(false);
            KisPart::instance()->addDocument(doc);
            mw()->addViewAndNotifyLoadingCompleted(doc);
        });
        m_steps.append([this] { shot(QStringLiteral("02-canvas")); });
        m_steps.append([this] { logPerf(QStringLiteral("classic-canvas")); });
        m_steps.append([this] { setSize(891, 411, QStringLiteral("classic-landscape")); });
        m_steps.append([this] { shot(QStringLiteral("02-canvas")); });
        m_steps.append([this] {
            logPerf(QStringLiteral("classic-end"));
            log(QStringLiteral("done"));
            m_log.close();
            QApplication::exit(0);
        });
    }

    void buildSteps()
    {
        m_steps.append([this] { logPerf(QStringLiteral("start")); });
        m_steps.append([this] { setSize(411, 891, QStringLiteral("portrait")); });
        addScreens();
        addDialogs();
        addPanelTimings();
        m_steps.append([this] { logPerf(QStringLiteral("after-portrait")); });
        m_steps.append([this] { setSize(891, 411, QStringLiteral("landscape")); });
        addScreens();
        addDialogs();
        m_steps.append([this] {
            if (m_shell) {
                m_shell->dumpInventory(QDir(m_dir).filePath(QStringLiteral("runtime-inventory.csv")));
                log(QStringLiteral("inventory written"));
            }
        });
        m_steps.append([this] {
            logPerf(QStringLiteral("end"));
            log(QStringLiteral("done"));
            m_log.close();
            QApplication::exit(0);
        });
    }

    void next()
    {
        if (m_index >= m_steps.size()) {
            return;
        }
        const int index = m_index++;
        m_extraDelay = 0;
        m_steps[index]();
        // Give layouts, animations and the canvas time to settle; steps that
        // open a window wait for it to be grabbed and closed.
        QTimer::singleShot(1200 + m_extraDelay, this, [this] {
            next();
        });
    }

    QPointer<Shell> m_shell;
    QString m_dir;
    QString m_orientation = QStringLiteral("portrait");
    QFile m_log;
    QVector<Step> m_steps;
    int m_index = 0;
    int m_extraDelay = 0;
};

} // namespace

void startTestDriverIfRequested(Shell *shell)
{
    const QByteArray dir = qgetenv("KRITA_MOBILE_SCREENSHOTS");
    if (!shell || dir.isEmpty() || shell->findChild<QObject *>(QStringLiteral("mobileTestDriver"))) {
        return;
    }
    TestDriver *driver = new TestDriver(shell, QString::fromLocal8Bit(dir));
    driver->setObjectName(QStringLiteral("mobileTestDriver"));
}

} // namespace mobileui
