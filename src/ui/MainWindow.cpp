#include "ui/MainWindow.h"
#include "core/Account.h"
#include "core/ClipLibrary.h"
#include "core/HotkeyManager.h"
#include "core/MediaTools.h"
#include "core/Settings.h"
#include "core/Theme.h"
#include "core/WebUploader.h"
#include "engine/Recorder.h"
#include "ui/Art.h"
#include "ui/EditorPage.h"
#include "ui/Pages.h"
#include "ui/Toast.h"
#include "BuildInfo.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDateTime>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSoundEffect>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

namespace kl {

class SidebarLogo : public QWidget {
public:
    SidebarLogo() { setFixedHeight(118); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        art::paintLens(p, QRectF(width() / 2.0 - 34, 8, 68, 68), 0.25, 8);
        art::paintWordmark(p, QRectF(0, 80, width(), 26), 15);
        art::paintDivider(p, QRectF(30, 108, width() - 60, 8), QColor(184, 178, 191, 120));
    }
};

MainWindow::MainWindow() {
    setWindowTitle(KL_APP_NAME);
    setWindowIcon(QIcon(QPixmap::fromImage(art::lensImage(256))));
    setMinimumSize(1100, 700);
    auto *root = new QWidget;
    root->setObjectName("Root");
    auto *rl = new QHBoxLayout(root);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);

    // ---- sidebar
    auto *side = new QWidget;
    side->setObjectName("Sidebar");
    side->setAttribute(Qt::WA_StyledBackground);
    side->setFixedWidth(222);
    auto *sl = new QVBoxLayout(side);
    sl->setContentsMargins(12, 10, 12, 14);
    sl->setSpacing(3);
    sl->addWidget(new SidebarLogo);
    auto *group = new QButtonGroup(this);
    struct Nav { QString id, label, glyph; };
    const QList<Nav> nav = {
        {"home", tr("Home"), "☾"}, {"library", tr("Library"), "▤"}, {"games", tr("Games"), "♜"}, {"capture", tr("Capture"), "◉"},
        {"replay", tr("Instant Replay"), "↺"}, {"clips", tr("Clips"), "▶"}, {"screenshots", tr("Screenshots"), "▣"},
        {"editor", tr("Video Editor"), "✂"}, {"favorites", tr("Favorites"), "★"}, {"highlights", tr("Highlights"), "✦"},
        {"account", tr("Sign in"), "◈"}, {"settings", tr("Settings"), "⚙"}, {"plugins", tr("Plugins"), "⧉"}};
    for (const auto &n : nav) {
        if (n.id == "account") sl->addStretch();
        auto *b = new QPushButton(QStringLiteral("%1   %2").arg(n.glyph, n.label));
        b->setObjectName("NavButton");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        group->addButton(b);
        sl->addWidget(b);
        m_nav.insert(n.id, b);
        connect(b, &QPushButton::clicked, this, [this, id = n.id] { showPage(id); });
    }
    rl->addWidget(side);

    // ---- right side: top bar + pages
    auto *main = new QWidget;
    auto *ml = new QVBoxLayout(main);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(0);
    auto *top = new QWidget;
    top->setObjectName("TopBar");
    top->setAttribute(Qt::WA_StyledBackground);
    auto *tl = new QHBoxLayout(top);
    tl->setContentsMargins(20, 10, 20, 10);
    auto *pill = new QWidget;
    pill->setObjectName("StatusPill");
    pill->setAttribute(Qt::WA_StyledBackground);
    auto *pl = new QHBoxLayout(pill);
    pl->setContentsMargins(12, 4, 14, 4);
    m_dot = new QLabel;
    m_dot->setFixedSize(12, 12);
    pl->addWidget(m_dot);
    auto *st = new QVBoxLayout;
    st->setSpacing(0);
    m_statusText = new QLabel;
    m_statusText->setObjectName("StatusText");
    m_statusSub = new QLabel;
    m_statusSub->setObjectName("StatusSub");
    st->addWidget(m_statusText);
    st->addWidget(m_statusSub);
    pl->addLayout(st);
    tl->addWidget(pill);
    tl->addStretch();
    m_saveBtn = new QPushButton;
    m_saveBtn->setObjectName("Primary");
    m_recBtn = new QPushButton;
    m_shotBtn = new QPushButton;
    m_pauseBtn = new QPushButton;
    for (QPushButton *b : {m_saveBtn, m_recBtn, m_shotBtn, m_pauseBtn}) { b->setCursor(Qt::PointingHandCursor); tl->addWidget(b); }
    auto &rec = Recorder::instance();
    connect(m_saveBtn, &QPushButton::clicked, &rec, [] { Recorder::instance().saveClip(); });
    connect(m_recBtn, &QPushButton::clicked, &rec, &Recorder::toggleRecording);
    connect(m_shotBtn, &QPushButton::clicked, &rec, &Recorder::takeScreenshot);
    connect(m_pauseBtn, &QPushButton::clicked, &rec, [] { Recorder::instance().setBufferPaused(!Recorder::instance().bufferPausedByUser()); });
    ml->addWidget(top);

    m_stack = new QStackedWidget;
    ml->addWidget(m_stack, 1);
    rl->addWidget(main, 1);
    setCentralWidget(root);

    auto addPage = [this](const QString &id, QWidget *w) {
        m_pages.insert(id, w);
        m_stack->addWidget(w);
        if (auto *p = qobject_cast<Page *>(w)) {
            connect(p, &Page::openClip, this, &MainWindow::openClip);
            connect(p, &Page::navigate, this, &MainWindow::showPage);
        }
    };
    ClipFilter clips; clips.kinds = {"clip", "trim", "recording"};
    ClipFilter shots; shots.kinds = {"screenshot"};
    ClipFilter favs; favs.favoritesOnly = true;
    addPage("home", new HomePage);
    addPage("library", new LibraryPage(tr("Library"), tr("Every clip, session recording and screenshot, newest first. Double-click to open."), {}, true));
    addPage("games", new GamesPage);
    addPage("capture", new CapturePage);
    addPage("replay", new ReplayPage);
    addPage("clips", new LibraryPage(tr("Clips"), tr("Your saved clips, trims and session recordings."), clips, false));
    addPage("screenshots", new LibraryPage(tr("Screenshots"), tr("Press %1 in a game to take one.").arg(HotkeyManager::instance().displayText("screenshot")), shots, false));
    m_editor = new EditorPage;
    connect(m_editor, &EditorPage::openClip, this, &MainWindow::openClip);
    addPage("editor", m_editor);
    addPage("favorites", new LibraryPage(tr("Favorites"), tr("Clips you starred. Right-click any clip to add it."), favs, false));
    addPage("highlights", new HighlightsPage);
    addPage("account", new AccountPage);
    addPage("settings", new SettingsPage);
    addPage("plugins", new PluginsPage);
    showPage(qEnvironmentVariable("KLIPIT_PAGE", "home"));   // KLIPIT_PAGE / KLIPIT_OPEN: start on a page or clip (used for screenshots)
    if (!qEnvironmentVariable("KLIPIT_OPEN").isEmpty()) QTimer::singleShot(500, this, [this] { openClip(qEnvironmentVariable("KLIPIT_OPEN")); });

    m_sound = new QSoundEffect(this);
    m_sound->setSource(QUrl("qrc:/sounds/clip.wav"));
    m_sound->setVolume(0.5f);   // the clip beep plays at 50 % volume

    connect(&rec, &Recorder::stateChanged, this, &MainWindow::refreshStatus);
    connect(&rec, &Recorder::clipSaved, this, &MainWindow::onClipSaved);

    // ---- KlipIt website account
    auto updateAccountNav = [this] {
        const auto &a = Account::instance();
        if (QPushButton *b = m_nav.value("account"))
            b->setText(QStringLiteral("◈   %1").arg(a.signedIn() ? b->fontMetrics().elidedText(a.name(), Qt::ElideRight, 140) : tr("Sign in")));
    };
    connect(&Account::instance(), &Account::changed, this, updateAccountNav);
    updateAccountNav();
    connect(&Account::instance(), &Account::signInFinished, this, [this](bool ok, const QString &msg) {
        if (ok) {
            Toast::show(tr("Signed in"), tr("Clips you save now show up in My clips on your website."));
            showPage("account");
            showNormal();
            raise();
            activateWindow();
        } else {
            Toast::show(tr("Sign-in didn't finish"), msg);
        }
    });
    connect(&WebUploader::instance(), &WebUploader::sent, this, [](const QString &, const QString &title) {
        if (Settings::instance().b("general.notifications")) Toast::show(tr("Sent to your website"), title);
    });
    connect(&WebUploader::instance(), &WebUploader::failed, this, [](const QString &, const QString &title, const QString &msg) {
        Toast::show(tr("Couldn't send “%1”").arg(title), msg);
    });
    connect(&rec, &Recorder::recordingSaved, this, [this](const QString &path, const QString &) {
        if (Settings::instance().b("general.notifications"))
            Toast::show(tr("Recording saved"), QFileInfo(path).completeBaseName(), QPixmap::fromImage(media::thumbnail(path, 2.0, 240)));
        refreshStatus();
    });
    connect(&rec, &Recorder::screenshotSaved, this, [](const QString &path, const QString &game) {
        if (Settings::instance().b("general.notifications")) Toast::show(tr("Screenshot saved"), game, QPixmap(path).scaledToWidth(240));
    });
    connect(&rec, &Recorder::notice, this, [](const QString &t, const QString &x) {
        if (Settings::instance().b("general.notifications")) Toast::show(t, x);
    });
    connect(&rec, &Recorder::problem, this, [this](const QString &m) {
        Toast::show(tr("Something went wrong"), m);
        refreshStatus();
    });
    auto *tick = new QTimer(this);   // recording timer in the status pill
    connect(tick, &QTimer::timeout, this, [this] { if (Recorder::instance().recordingActive()) refreshStatus(); });
    tick->start(1000);
    buildTray();
    refreshStatus();
    const QByteArray geo = QByteArray::fromBase64(Settings::instance().s("window.geometry").toLatin1());
    if (!geo.isEmpty()) restoreGeometry(geo); else resize(1320, 820);
}

void MainWindow::showEvent(QShowEvent *e) {
    QMainWindow::showEvent(e);
#ifdef _WIN32
    // dark title bar + matching caption colour (Windows 10 20H1+ / 11)
    const BOOL dark = theme::current().light ? FALSE : TRUE;
    DwmSetWindowAttribute(HWND(winId()), 20, &dark, sizeof(dark));
    const QColor bg = theme::current().panel;
    const COLORREF caption = RGB(bg.red(), bg.green(), bg.blue());
    DwmSetWindowAttribute(HWND(winId()), 35, &caption, sizeof(caption));
#endif
}

void MainWindow::showPage(const QString &id) {
    if (QWidget *w = m_pages.value(id)) m_stack->setCurrentWidget(w);
    if (QPushButton *b = m_nav.value(id)) b->setChecked(true);
    if (id == "games") if (auto *g = qobject_cast<GamesPage *>(m_pages.value(id))) g->refresh();
    if (id == "plugins") if (auto *p = qobject_cast<PluginsPage *>(m_pages.value(id))) p->refresh();
}

void MainWindow::openClip(const QString &path) {
    showPage("editor");
    m_editor->open(path);
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::onClipSaved(const QString &path, const QString &game, bool highlight) {
    auto &s = Settings::instance();
    if (s.b("general.clipSound")) m_sound->play();
    if (Account::instance().signedIn() && s.b("cloud.autoSend")) WebUploader::instance().send(path);
    if (s.b("general.notifications")) {
        const media::Info info = media::probe(path);
        Toast::show(highlight ? tr("Highlight saved") : tr("Clip saved"),
                    tr("%1 · %2").arg(game, media::formatDuration(info.duration)),
                    QPixmap::fromImage(media::thumbnail(path, qMax(0.0, info.duration - 2), 240)),
                    Recorder::instance().game().windowRect);
    }
    refreshStatus();
}

static QString keyName(const QString &id) {
    const Hotkey k = HotkeyManager::instance().binding(id);
    return k.isEmpty() ? QString() : QStringLiteral("  (%1)").arg(k.text);
}

void MainWindow::refreshStatus() {
    auto &r = Recorder::instance();
    const auto &pal = theme::current();
    QColor dot = pal.muted;
    if (r.recordingActive()) {
        const qint64 secs = (QDateTime::currentMSecsSinceEpoch() - r.recordingStartedMs()) / 1000;
        m_statusText->setText(tr("● Recording %1").arg(media::formatDuration(double(secs))));
        m_statusSub->setText(tr("%1 · saved in parts of up to 20 minutes").arg(r.gameName().isEmpty() ? tr("Screen") : r.gameName()));
        dot = pal.accent;
    } else if (r.bufferActive()) {
        m_statusText->setText(r.gameName().isEmpty() ? tr("Instant replay on") : tr("Instant replay · %1").arg(r.gameName()));
        m_statusSub->setText(tr("%1 · last %2 · %3").arg(r.captureMethod(), media::formatDuration(r.clipSeconds()), r.encoderName()));
        dot = QColor("#4CAF7A");
    } else if (r.bufferPausedByUser()) {
        m_statusText->setText(tr("Instant replay paused"));
        m_statusSub->setText(tr("Nothing is being recorded"));
    } else {
        m_statusText->setText(r.lastError().isEmpty() ? tr("Waiting for a game") : tr("Capture problem"));
        m_statusSub->setText(r.lastError().isEmpty() ? tr("Recording starts by itself when you play") : r.lastError().left(90));
        if (!r.lastError().isEmpty()) dot = pal.accent;
    }
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(dot);
    p.drawEllipse(1, 1, 10, 10);
    p.end();
    m_dot->setPixmap(pm);
    m_saveBtn->setText(tr("Save clip") + keyName("saveClip"));
    m_recBtn->setText((r.recordingActive() ? tr("Stop recording") : tr("Record")) + keyName("toggleRecording"));
    m_shotBtn->setText(tr("Screenshot") + keyName("screenshot"));
    m_pauseBtn->setText(r.bufferPausedByUser() ? tr("Resume replay") : tr("Pause replay"));
    if (m_tray) {
        m_tray->setToolTip(QStringLiteral("KlipIt · %1").arg(m_statusText->text()));
        m_trayRec->setText(r.recordingActive() ? tr("Stop recording") : tr("Start recording"));
        m_trayPause->setText(r.bufferPausedByUser() ? tr("Resume instant replay") : tr("Pause instant replay"));
    }
}

void MainWindow::buildTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    m_tray = new QSystemTrayIcon(QIcon(QPixmap::fromImage(art::lensImage(64))), this);
    auto *menu = new QMenu(this);
    menu->addAction(tr("Open KlipIt"), this, [this] { showNormal(); raise(); activateWindow(); });
    menu->addSeparator();
    menu->addAction(tr("Save clip"), &Recorder::instance(), [] { Recorder::instance().saveClip(); });
    m_trayRec = menu->addAction(tr("Start recording"), &Recorder::instance(), &Recorder::toggleRecording);
    menu->addAction(tr("Take screenshot"), &Recorder::instance(), &Recorder::takeScreenshot);
    m_trayPause = menu->addAction(tr("Pause instant replay"), &Recorder::instance(),
                                  [] { Recorder::instance().setBufferPaused(!Recorder::instance().bufferPausedByUser()); });
    menu->addSeparator();
    menu->addAction(tr("Quit KlipIt"), this, [this] { m_quitting = true; close(); });
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) { showNormal(); raise(); activateWindow(); }
    });
    m_tray->show();
}

void MainWindow::closeEvent(QCloseEvent *e) {
    Settings::instance().set("window.geometry", QString::fromLatin1(saveGeometry().toBase64()));
    Settings::instance().save();
    if (!m_quitting && m_tray && Settings::instance().b("general.closeToTray")) {
        hide();
        e->ignore();
        if (!m_trayHintShown) {
            m_trayHintShown = true;
            m_tray->showMessage(tr("KlipIt is still running"), tr("It keeps recording your games from the tray. Right-click the icon to quit."),
                                QSystemTrayIcon::Information, 4000);
        }
        return;
    }
    if (Recorder::instance().recordingActive() &&
        QMessageBox::question(this, tr("Quit KlipIt"), tr("A recording is running. Stop it, save it and quit?")) != QMessageBox::Yes) {
        m_quitting = false;
        e->ignore();
        return;
    }
    e->accept();
    qApp->quit();
}

} // namespace kl
