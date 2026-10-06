/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "MobileTestDriver.h"
#include "MobileCommandBrowser.h"
#include "MobileHub.h"
#include "MobileSheet.h"
#include "MobileShell.h"

#include <KisMainWindow.h>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QPointer>
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
        buildSteps();
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
        m_steps.append([this] { shot(QStringLiteral("03-editor")); });
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
    // scheduled before triggering.
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
            QTimer::singleShot(3000, this, [this, actionName] {
                QWidget *w = QApplication::activeModalWidget();
                if (!w) {
                    QWidget *active = QApplication::activeWindow();
                    if (active && active != mw()) {
                        w = active;
                    }
                }
                if (!w) {
                    log(QStringLiteral("dialog %1: no window appeared").arg(actionName));
                    return;
                }
                QString safe = actionName;
                safe.replace(QLatin1Char(' '), QLatin1Char('_'));
                const QString path = QDir(m_dir).filePath(QStringLiteral("dialog-%1.png").arg(safe));
                const bool ok = w->grab().save(path);
                log(QStringLiteral("dialog %1: %2 %3x%4 min %5x%6 %7")
                        .arg(actionName, QString::fromLatin1(w->metaObject()->className()))
                        .arg(w->width())
                        .arg(w->height())
                        .arg(w->minimumSizeHint().width())
                        .arg(w->minimumSizeHint().height())
                        .arg(ok ? QStringLiteral("saved") : QStringLiteral("FAILED")));
                if (QDialog *dialog = qobject_cast<QDialog *>(w)) {
                    dialog->reject();
                } else {
                    w->close();
                }
            });
            action->trigger();
        });
    }

    void buildSteps()
    {
        m_steps.append([this] { setSize(411, 891, QStringLiteral("portrait")); });
        addScreens();
        for (const char *name : {"file_new", "options_configure", "imagesize", "canvassize", "image_properties",
                                 "layer_properties", "layer_style", "krita_filter_gaussian blur", "render_animation"}) {
            addDialog(QString::fromLatin1(name));
        }
        m_steps.append([this] { setSize(891, 411, QStringLiteral("landscape")); });
        addScreens();
        m_steps.append([this] {
            if (m_shell) {
                m_shell->dumpInventory(QDir(m_dir).filePath(QStringLiteral("runtime-inventory.csv")));
                log(QStringLiteral("inventory written"));
            }
        });
        m_steps.append([this] {
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
        m_steps[index]();
        // Give layouts, animations and the canvas time to settle.
        QTimer::singleShot(1200, this, [this] {
            next();
        });
    }

    QPointer<Shell> m_shell;
    QString m_dir;
    QString m_orientation = QStringLiteral("portrait");
    QFile m_log;
    QVector<Step> m_steps;
    int m_index = 0;
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
