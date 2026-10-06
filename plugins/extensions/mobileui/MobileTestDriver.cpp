/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "MobileTestDriver.h"
#include "MobileCommandBrowser.h"
#include "MobileConfig.h"
#include "MobileHub.h"
#include "MobilePerf.h"
#include "MobileSheet.h"
#include "MobileShell.h"

#include <KisDocument.h>
#include <KisMainWindow.h>
#include <KisPart.h>
#include <KisViewManager.h>
#include <kactioncollection.h>
#include <KoColor.h>
#include <KoColorSpaceRegistry.h>
#include <kis_config.h>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QImage>
#include <QDialog>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QLayout>
#include <QPixmap>
#include <QPointer>
#include <QScopedPointer>
#include <QSet>
#include <QTextStream>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>

#ifdef Q_OS_ANDROID
#include <QtAndroid>
#endif

namespace mobileui {

namespace {

// Aborts the process when the GUI thread stops making progress, so a hang
// produces a backtrace (Krita runs under gdb in CI) instead of a silent
// timeout. The test driver beats on every step of the run.
class HangDetector
{
public:
    explicit HangDetector(const QString &logPath)
        : m_logPath(logPath.toLocal8Bit())
    {
        m_thread = std::thread([this] {
            int lastBeat = -1;
            int quietSeconds = 0;
            while (!m_stop) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                const int beat = m_beat.load();
                if (beat != lastBeat) {
                    lastBeat = beat;
                    quietSeconds = 0;
                    continue;
                }
                if (++quietSeconds >= 120 && !m_stop) {
                    if (FILE *f = fopen(m_logPath.constData(), "a")) {
                        fprintf(f, "HANG: no progress for 120 s, aborting for a backtrace\n");
                        fclose(f);
                    }
                    std::abort();
                }
            }
        });
    }
    ~HangDetector()
    {
        m_stop = true;
        m_thread.join();
    }
    void beat() { ++m_beat; }

private:
    QByteArray m_logPath;
    std::atomic<int> m_beat{0};
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

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
        m_hang.reset(new HangDetector(m_log.fileName()));
        // Beats from the event loop: a nested dialog loop still counts as
        // progress, a blocked GUI thread does not.
        QTimer *heartbeat = new QTimer(this);
        connect(heartbeat, &QTimer::timeout, this, [this] {
            m_hang->beat();
        });
        heartbeat->start(1000);
        log(QStringLiteral("interface: %1 (smallest side %2 dp)")
                .arg(shell->isActive() ? QStringLiteral("phone") : QStringLiteral("classic"))
                .arg(config::smallestScreenSideDp(shell->mainWindow())));
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
#ifdef Q_OS_ANDROID
        // Also in logcat, for runs where the file can't be fetched.
        qInfo().noquote() << "Krita Mobile test:" << message;
#endif
    }

    KisMainWindow *mw() const { return m_shell ? m_shell->mainWindow() : nullptr; }

    // Screenshots are saved at the logical (dp) size: phone screens have a
    // device pixel ratio of 2.5-3.5, and full-resolution images would make
    // the CI results branch huge.
    static bool saveGrab(QWidget *widget, const QString &path)
    {
        QPixmap pixmap = widget->grab();
        const qreal ratio = pixmap.devicePixelRatio();
        if (ratio > 1.01) {
            pixmap = pixmap.scaled((QSizeF(pixmap.size()) / ratio).toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        return pixmap.save(path);
    }

    void shot(const QString &name)
    {
        if (!mw()) {
            return;
        }
        const QString path = QDir(m_dir).filePath(QStringLiteral("%1-%2.png").arg(m_orientation, name));
        log(QStringLiteral("screenshot %1 %2").arg(path, saveGrab(mw(), path) ? QStringLiteral("ok") : QStringLiteral("FAILED")));
    }

    void setSize(int w, int h, const QString &orientation)
    {
        m_orientation = orientation;
#ifdef Q_OS_ANDROID
        // Android windows fill the screen; rotate the activity instead
        // (ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE = 0, PORTRAIT = 1).
        QtAndroid::runOnAndroidThread([w, h] {
            QtAndroid::androidActivity().callMethod<void>("setRequestedOrientation", "(I)V", w > h ? 0 : 1);
        });
        return;
#endif
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
                        QWidget *parent = w->parentWidget();
                        const bool inLayout = parent && parent->layout() && parent->layout()->indexOf(w) >= 0;
                        log(QStringLiteral("probe %1,%2: %3 '%4' %5,%6 %7x%8 tip='%9' parent=%10 inLayout=%11")
                                .arg(probe.x())
                                .arg(probe.y())
                                .arg(QString::fromLatin1(w->metaObject()->className()), w->objectName())
                                .arg(g.x())
                                .arg(g.y())
                                .arg(g.width())
                                .arg(g.height())
                                .arg(w->toolTip(), parent ? QString::fromLatin1(parent->metaObject()->className()) : QString())
                                .arg(inLayout));
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
                const Qt::WindowType type = w->windowType();
                if (w->isVisible() && !before.contains(w) && w != mw() && type != Qt::ToolTip && type != Qt::Popup
                    && (type == Qt::Dialog || type == Qt::Window || type == Qt::Tool || qobject_cast<QDialog *>(w))) {
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
        const bool ok = saveGrab(found, path);
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
        // The desktop interface must stay exactly as upstream.
        m_steps.append([this] { setSize(1280, 960, QStringLiteral("classic-desktop")); });
        m_steps.append([this] { shot(QStringLiteral("02-canvas")); });
        // Reproduction of a problem the command walk found, in Krita's own
        // interface (without the phone interface): copy the background
        // layer, then "Paste into New Image".
        if (config::testSetting("KRITA_MOBILE_REPRO") == QLatin1String("paste_new")) {
            for (const char *name : {"activatePreviousLayer", "edit_copy", "paste_new"}) {
                m_steps.append([this, name] {
                    QAction *action = mw() ? mw()->viewManager()->actionCollection()->action(QString::fromLatin1(name)) : nullptr;
                    log(QStringLiteral("repro: %1 %2").arg(QString::fromLatin1(name),
                                                           !action ? QStringLiteral("missing")
                                                                   : action->isEnabled() ? QStringLiteral("triggering")
                                                                                         : QStringLiteral("disabled")));
                    if (action && action->isEnabled()) {
                        action->trigger();
                    }
                    log(QStringLiteral("repro: %1 returned").arg(QString::fromLatin1(name)));
                });
            }
        }
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
        if (!config::testSetting("KRITA_MOBILE_ACTION_WALK").isEmpty()) {
            addActionWalk();
        }
        m_steps.append([this] {
            // The walk modified the documents; nothing must ask to save them.
            for (QPointer<KisDocument> doc : KisPart::instance()->documents()) {
                if (doc) {
                    doc->setModified(false);
                }
            }
            logPerf(QStringLiteral("end"));
            log(QStringLiteral("done"));
            m_log.close();
            QApplication::exit(0);
        });
    }

    // ---- action walk -------------------------------------------------
    // Triggers every command listed in the phone interface's command list,
    // the same way tapping its row does, closes whatever window it opens,
    // and records the result in walk.csv. Checkable commands are triggered
    // twice so their state is restored. A crash names the last command in
    // testdriver.log.

    static bool skippedInWalk(const QString &name, QString *reason)
    {
        static const QHash<QString, QString> skipped = {
            {QStringLiteral("file_quit"), QStringLiteral("quits Krita")},
            {QStringLiteral("file_close"), QStringLiteral("closes the test document")},
            {QStringLiteral("file_close_all"), QStringLiteral("closes the test document")},
            {QStringLiteral("view_newwindow"), QStringLiteral("opens a second main window")},
            {QStringLiteral("view_detached_canvas"), QStringLiteral("moves the canvas to its own window")},
            {QStringLiteral("reset_configurations"), QStringLiteral("resets all settings")},
            {QStringLiteral("render_animation_again"), QStringLiteral("renders files with the last settings")},
            {QStringLiteral("help_contents"), QStringLiteral("opens a web browser")},
            {QStringLiteral("help_report_bug"), QStringLiteral("opens a web browser")},
        };
#ifdef Q_OS_ANDROID
        // Opens Google Play's subscription page in another app; Krita goes
        // to the background and the test can't bring it back.
        if (name == QLatin1String("manage_subscriptions")) {
            *reason = QStringLiteral("opens Google Play in another app");
            return true;
        }
        // The Android file picker is a separate app the test can't close.
        if (name.contains(QLatin1String("import")) || name.contains(QLatin1String("export"))
            || name.startsWith(QLatin1String("file_open")) || name.startsWith(QLatin1String("file_save"))
            || name.startsWith(QLatin1String("save_"))) {
            *reason = QStringLiteral("opens the Android file picker");
            return true;
        }
#endif
        if (name.startsWith(QLatin1String("mobileui_mode_"))) {
            *reason = QStringLiteral("switches the interface");
            return true;
        }
        const auto it = skipped.constFind(name);
        if (it != skipped.constEnd()) {
            *reason = it.value();
            return true;
        }
        return false;
    }

    void addActionWalk()
    {
        m_steps.append([this] {
            if (!m_shell || !m_shell->commandBrowser()) {
                return;
            }
            m_shell->closePanel();
            m_shell->hideHub();
            QSet<QString> seen;
            for (const CommandBrowser::Entry &entry : m_shell->commandBrowser()->allEntries()) {
                if (entry.action && !entry.action->objectName().isEmpty() && !seen.contains(entry.action->objectName())) {
                    seen.insert(entry.action->objectName());
                    m_walk.append({entry.action, entry.action->objectName(), entry.path});
                }
            }
            m_walkFile.setFileName(QDir(m_dir).filePath(QStringLiteral("walk.csv")));
            m_walkFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
            QTextStream(&m_walkFile) << "id,result,detail,ms\n";
            m_walkBaseline.clear();
            for (QWidget *w : QApplication::topLevelWidgets()) {
                if (w->isVisible()) {
                    m_walkBaseline.insert(w);
                }
            }
            log(QStringLiteral("walk: %1 commands").arg(m_walk.size()));
            m_walkWatchdog = new QTimer(this);
            m_walkWatchdog->setInterval(1000);
            connect(m_walkWatchdog, &QTimer::timeout, this, [this] {
                closeStrayWindows();
            });
            m_walkWatchdog->start();
            m_extraDelay = -1;
            QTimer::singleShot(0, this, [this] {
                walkNext();
            });
        });
    }

    QStringList closeStrayWindows()
    {
        QStringList closed;
        for (int pass = 0; pass < 4; ++pass) {
            QWidget *modal = QApplication::activeModalWidget();
            if (!modal || modal == mw()) {
                break;
            }
            closed << QString::fromLatin1(modal->metaObject()->className());
            if (QDialog *dialog = qobject_cast<QDialog *>(modal)) {
                dialog->reject();
            } else {
                modal->close();
            }
            if (QApplication::activeModalWidget() == modal) {
                modal->hide();
            }
        }
        for (QWidget *w : QApplication::topLevelWidgets()) {
            const Qt::WindowType type = w->windowType();
            if (!w->isVisible() || w == mw() || m_walkBaseline.contains(w) || type == Qt::ToolTip) {
                continue;
            }
            closed << QString::fromLatin1(w->metaObject()->className());
            if (QDialog *dialog = qobject_cast<QDialog *>(w)) {
                dialog->reject();
            } else {
                w->close();
            }
            if (w->isVisible()) {
                w->hide();
            }
        }
        return closed;
    }

    void writeWalkRow(const QString &id, const QString &result, const QString &detail, qint64 ms)
    {
        // Explicit QString return type: with QStringBuilder an auto return
        // type would keep a reference to the destroyed local copy.
        auto field = [](QString v) -> QString {
            v.replace(QLatin1Char('"'), QLatin1String("\"\""));
            return QLatin1Char('"') + v + QLatin1Char('"');
        };
        QTextStream(&m_walkFile) << field(id) << ',' << field(result) << ',' << field(detail) << ',' << ms << '\n';
        m_walkFile.flush();
    }

    void walkNext()
    {
        if (m_walkIndex >= m_walk.size()) {
            if (m_walkWatchdog) {
                m_walkWatchdog->stop();
            }
            closeStrayWindows();
            m_walkFile.close();
            log(QStringLiteral("walk: done"));
            QTimer::singleShot(1500, this, [this] {
                next();
            });
            return;
        }
        const WalkItem item = m_walk.at(m_walkIndex++);
        QString reason;
        if (!item.action) {
            writeWalkRow(item.id, QStringLiteral("gone"), QString(), 0);
            walkNext();
            return;
        }
        if (skippedInWalk(item.id, &reason)) {
            writeWalkRow(item.id, QStringLiteral("skipped"), reason, 0);
            walkNext();
            return;
        }
        if (!item.action->isEnabled()) {
            writeWalkRow(item.id, QStringLiteral("disabled"), QStringLiteral("not available in this state"), 0);
            walkNext();
            return;
        }
        log(QStringLiteral("walk %1/%2: %3").arg(m_walkIndex).arg(m_walk.size()).arg(item.id));
        if (item.id.contains(QLatin1String("paste"))) {
            // Copying a whole layer whose default pixel isn't transparent
            // (the white background) puts an unbounded area on the clipboard
            // and Krita's own "Paste into New Image" then hangs (bug log
            // #15, reproduced without the phone interface). Paste commands
            // get a small known image instead.
            QImage image(64, 64, QImage::Format_ARGB32);
            image.fill(QColor(200, 60, 60));
            QApplication::clipboard()->setImage(image);
        }
        m_walkTimer.start();
        const bool checkable = item.action->isCheckable();
        m_shell->commandBrowser()->activateForTest(item.action);
        if (checkable && item.action) {
            m_shell->commandBrowser()->activateForTest(item.action);
        }
        QPointer<QAction> action = item.action;
        QTimer::singleShot(checkable ? 80 : 600, this, [this, item, action] {
            const qint64 ms = m_walkTimer.elapsed();
            const QStringList closed = closeStrayWindows();
            if (m_shell) {
                m_shell->closePanel();
            }
            writeWalkRow(item.id, QStringLiteral("ok"),
                         closed.isEmpty() ? QString() : QStringLiteral("opened ") + closed.join(QLatin1Char(' ')), ms);
            // Let deferred work (and dialogs being torn down) finish.
            QTimer::singleShot(60, this, [this] {
                walkNext();
            });
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
        if (m_extraDelay < 0) {
            // The step continues on its own and calls next() when done.
            return;
        }
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
    QScopedPointer<HangDetector> m_hang;

    struct WalkItem {
        QPointer<QAction> action;
        QString id;
        QString path;
    };
    QVector<WalkItem> m_walk;
    int m_walkIndex = 0;
    QFile m_walkFile;
    QSet<QWidget *> m_walkBaseline;
    QPointer<QTimer> m_walkWatchdog;
    QElapsedTimer m_walkTimer;
};

} // namespace

void startTestDriverIfRequested(Shell *shell)
{
    QString dir = config::testSetting("KRITA_MOBILE_SCREENSHOTS");
#ifdef Q_OS_ANDROID
    qInfo().noquote() << "Krita Mobile test: control directory" << config::androidTestDirectory()
                      << (dir.isEmpty() ? "no test run requested" : "test run requested");
#endif
    // On Android the value of the file is ignored: the output goes next to
    // it, where "adb pull" can fetch it.
    if (!dir.isEmpty() && qgetenv("KRITA_MOBILE_SCREENSHOTS").isEmpty() && !config::androidTestDirectory().isEmpty()) {
        dir = config::androidTestDirectory() + QStringLiteral("/out");
    }
    if (!shell || dir.isEmpty() || shell->findChild<QObject *>(QStringLiteral("mobileTestDriver"))) {
        return;
    }
    TestDriver *driver = new TestDriver(shell, dir);
    driver->setObjectName(QStringLiteral("mobileTestDriver"));
}

} // namespace mobileui
