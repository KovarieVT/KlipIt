// KlipIt — entry point
#include "BuildInfo.h"
#include "core/Account.h"
#include "core/Autostart.h"
#include "core/ClipLibrary.h"
#include "core/GameDetector.h"
#include "core/HotkeyManager.h"
#include "core/Logger.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "core/Theme.h"
#include "engine/Recorder.h"
#include "ui/Art.h"
#include "ui/MainWindow.h"
#include "ui/Splash.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QUrl>

using namespace kl;

// The installer's theme page leaves its choice here for the first start.
static void applyInstallerChoices() {
    const QString f = Paths::dataRoot() + "/installer.json";
    QFile file(f);
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    auto &s = Settings::instance();
    const QString themeId = o.value("theme").toString();
    if (!themeId.isEmpty()) s.set("appearance.theme", theme::byId(themeId).id);
    if (o.contains("autostart")) { s.set("general.startWithWindows", o.value("autostart").toBool()); autostart::setEnabled(o.value("autostart").toBool()); }
    s.save();
    QFile::remove(f);
}

int main(int argc, char **argv) {
    Q_INIT_RESOURCE(resources);
#ifdef _WIN32
    // Windows' own video playback for the editor (Qt's FFmpeg backend would clash with the engine's FFmpeg).
    if (qEnvironmentVariableIsEmpty("QT_MEDIA_BACKEND")) qputenv("QT_MEDIA_BACKEND", "windows");
#endif
#ifdef __linux__
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "xcb");
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName(KL_APP_EXE);
    QApplication::setApplicationDisplayName(KL_APP_NAME);
    QApplication::setOrganizationName(KL_APP_ORG);
    QApplication::setApplicationVersion(KL_APP_VERSION);
    QApplication::setQuitOnLastWindowClosed(false);
    const bool startInTray = app.arguments().contains("--tray");

    // ---- one KlipIt at a time: a second launch just brings the window up
    const QString server = QStringLiteral("KlipIt-") + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"));
    {
        QLocalSocket probe;
        probe.connectToServer(server);
        if (probe.waitForConnected(300)) { probe.write("show"); probe.waitForBytesWritten(300); return 0; }
    }
    QLocalServer instance;
    QLocalServer::removeServer(server);
    instance.listen(server);

    Settings::instance().load(Paths::configFile());
    Account::instance().load();
    applyInstallerChoices();
    Logger::init();
    theme::loadFonts();
    theme::apply(app, Settings::instance().s("appearance.theme"));
    QApplication::setWindowIcon(QIcon(QPixmap::fromImage(art::lensImage(256))));

    Splash *splash = nullptr;
    if (!startInTray) {
        splash = new Splash(Settings::instance().b("general.animations"));
        splash->show();
        app.processEvents();
    }
    ClipLibrary::instance().setRoot(Settings::instance().s("clips.folder"));
    Logger::installObsLogHandler();
    QString err;
    const bool ok = Recorder::instance().startup([splash](int pct, const QString &msg) { if (splash) splash->setProgress(pct, msg); }, &err);
    if (!ok) {
        if (splash) splash->close();
        QMessageBox box(QMessageBox::Critical, KL_APP_NAME,
                        QObject::tr("KlipIt's capture engine couldn't start.\n\n%1\n\nUpdating your graphics driver usually fixes this.").arg(err));
        auto *logs = box.addButton(QObject::tr("Open log"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Close);
        box.exec();
        if (box.clickedButton() == static_cast<QAbstractButton *>(logs)) QDesktopServices::openUrl(QUrl::fromLocalFile(Logger::currentLogFile()));
        return 1;
    }

    auto &hk = HotkeyManager::instance();
    auto &rec = Recorder::instance();
    hk.registerAction("saveClip", QObject::tr("Save clip"), [&rec] { rec.saveClip(); });
    hk.registerAction("toggleRecording", QObject::tr("Start / stop recording"), [&rec] { rec.toggleRecording(); });
    hk.registerAction("screenshot", QObject::tr("Screenshot"), [&rec] { rec.takeScreenshot(); });
    hk.registerAction("toggleBuffer", QObject::tr("Pause / resume instant replay"), [&rec] { rec.setBufferPaused(!rec.bufferPausedByUser()); });

    auto *win = new MainWindow;
    hk.setHostWindow(win);
    app.installNativeEventFilter(&hk);
    hk.applyAll();
    GameDetector::instance().start();
    QObject::connect(&instance, &QLocalServer::newConnection, win, [win, &instance] {
        if (QLocalSocket *s = instance.nextPendingConnection()) { s->deleteLater(); win->showNormal(); win->raise(); win->activateWindow(); }
    });
    if (splash) splash->finish(win);
    if (!hk.failed().isEmpty())
        QTimer::singleShot(2500, win, [win] {
            QMessageBox::warning(win, KL_APP_NAME, QObject::tr("Another program is already using one of KlipIt's hotkeys. "
                                                              "Pick a different key in Settings → Hotkeys."));
        });

    const int rc = app.exec();
    GameDetector::instance().stop();
    hk.suspend(true);
    Recorder::instance().shutdown();
    Settings::instance().save();
    delete win;
    qInfo("Clean exit (%d)", rc);
    Logger::shutdown();
    return rc;
}
