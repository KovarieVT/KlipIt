#include "ui/Pages.h"
#include "core/Account.h"
#include "core/Autostart.h"
#include "core/ClipLibrary.h"
#include "core/GameDetector.h"
#include "core/HotkeyManager.h"
#include "core/Logger.h"
#include "core/MediaTools.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "core/Theme.h"
#include "core/WebUploader.h"
#include "engine/Recorder.h"
#include "ui/Art.h"
#include "ui/HotkeyEdit.h"
#include "ui/MonitorPicker.h"
#include "BuildInfo.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

namespace kl {

static Settings &S() { return Settings::instance(); }
static void saveAndApply(bool engine) {
    S().save();
    if (engine) Recorder::instance().applySettings();
}
static void repolish(QWidget *w) { w->style()->unpolish(w); w->style()->polish(w); }

// ------------------------------------------------------------------ helpers
static QCheckBox *check(const QString &label, const QString &key, bool engine, std::function<void(bool)> extra = {}) {
    auto *c = new QCheckBox(label);
    c->setChecked(S().b(key));
    QObject::connect(c, &QCheckBox::toggled, c, [key, engine, extra](bool on) {
        S().set(key, on);
        saveAndApply(engine);
        if (extra) extra(on);
    });
    return c;
}

static QComboBox *combo(const QString &key, const QList<QPair<QString, QVariant>> &items, bool engine) {
    auto *c = new QComboBox;
    for (const auto &[label, value] : items) c->addItem(label, value);
    const int i = c->findData(S().get(key));
    c->setCurrentIndex(i < 0 ? 0 : i);
    QObject::connect(c, &QComboBox::activated, c, [c, key, engine](int idx) {
        S().set(key, c->itemData(idx));
        saveAndApply(engine);
    });
    return c;
}

static QPushButton *primary(const QString &text) {
    auto *b = new QPushButton(text);
    b->setObjectName("Primary");
    return b;
}

static QString keyText(const QString &id) {
    return HotkeyManager::instance().displayText(id);
}

// ------------------------------------------------------------------ Page
Page::Page(const QString &title, const QString &subtitle, bool scroll, QWidget *parent) : QWidget(parent) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *content = new QWidget;
    m_body = new QVBoxLayout(content);
    m_body->setContentsMargins(28, 22, 28, 22);
    m_body->setSpacing(14);
    auto *t = new QLabel(title);
    t->setObjectName("PageTitle");
    m_body->addWidget(t);
    if (!subtitle.isEmpty()) {
        auto *s = new QLabel(subtitle);
        s->setObjectName("PageSub");
        s->setWordWrap(true);
        m_body->addWidget(s);
    }
    if (scroll) {
        auto *sa = new QScrollArea;
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setWidget(content);
        outer->addWidget(sa);
    } else {
        outer->addWidget(content);
    }
}

QWidget *Page::card(QVBoxLayout **inner, const QString &title, const QString &hintText) {
    auto *w = new QWidget;
    w->setObjectName("Card");
    w->setAttribute(Qt::WA_StyledBackground);
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(18, 14, 18, 16);
    l->setSpacing(9);
    if (!title.isEmpty()) {
        auto *t = new QLabel(title);
        t->setObjectName("SectionTitle");
        l->addWidget(t);
    }
    if (!hintText.isEmpty()) l->addWidget(hint(hintText));
    *inner = l;
    return w;
}

QLabel *Page::hint(const QString &text) {
    auto *h = new QLabel(text);
    h->setObjectName("Hint");
    h->setWordWrap(true);
    return h;
}

// ------------------------------------------------------------------ Home
HomePage::HomePage(QWidget *parent) : Page(tr("Home"), {}, true, parent) {
    auto *hero = new QWidget;
    hero->setObjectName("Hero");
    hero->setAttribute(Qt::WA_StyledBackground);
    auto *hl = new QHBoxLayout(hero);
    hl->setContentsMargins(22, 18, 22, 18);
    auto *lens = new QLabel;
    lens->setPixmap(QPixmap::fromImage(art::lensImage(96, 0.35)));
    hl->addWidget(lens);
    auto *txt = new QVBoxLayout;
    m_state = new QLabel;
    m_state->setObjectName("PageTitle");
    m_detail = new QLabel;
    m_detail->setObjectName("Muted");
    m_detail->setWordWrap(true);
    m_hotkeys = new QLabel;
    m_hotkeys->setObjectName("Hint");
    txt->addWidget(m_state);
    txt->addWidget(m_detail);
    txt->addWidget(m_hotkeys);
    hl->addLayout(txt, 1);
    auto *btns = new QVBoxLayout;
    auto *save = primary(tr("Save clip"));
    auto *rec = new QPushButton(tr("Record"));
    auto *shot = new QPushButton(tr("Screenshot"));
    for (QPushButton *b : {save, rec, shot}) { b->setMinimumWidth(150); btns->addWidget(b); }
    hl->addLayout(btns);
    connect(save, &QPushButton::clicked, &Recorder::instance(), [] { Recorder::instance().saveClip(); });
    connect(rec, &QPushButton::clicked, &Recorder::instance(), &Recorder::toggleRecording);
    connect(shot, &QPushButton::clicked, &Recorder::instance(), &Recorder::takeScreenshot);
    m_body->addWidget(hero);

    auto *stats = new QHBoxLayout;
    auto stat = [&](QLabel *&value, const QString &name) {
        QVBoxLayout *l;
        QWidget *c = card(&l);
        value = new QLabel("0");
        value->setObjectName("BigNumber");
        l->addWidget(value);
        auto *n = new QLabel(name);
        n->setObjectName("Muted");
        l->addWidget(n);
        stats->addWidget(c);
    };
    stat(m_clips, tr("clips"));
    stat(m_sessions, tr("session recordings"));
    stat(m_shots, tr("screenshots"));
    stat(m_storage, tr("used on disk"));
    m_body->addLayout(stats);

    auto section = [&](const QString &title, const ClipFilter &f, const QString &page) {
        auto *row = new QHBoxLayout;
        auto *t = new QLabel(title);
        t->setObjectName("SectionTitle");
        row->addWidget(t);
        row->addStretch();
        auto *all = new QPushButton(tr("See all"));
        connect(all, &QPushButton::clicked, this, [this, page] { Q_EMIT navigate(page); });
        row->addWidget(all);
        m_body->addLayout(row);
        auto *g = new ClipGrid;
        g->setHorizontalStrip(true);
        g->setFilter(f);
        if (f.highlightsOnly) g->setEmptyText(tr("No highlights yet. When you get loud in a game, KlipIt saves the moment here by itself."));
        connect(g, &ClipGrid::openRequested, this, &Page::openClip);
        m_body->addWidget(g);
    };
    ClipFilter recent; recent.limit = 12;
    section(tr("Recent"), recent, "library");
    ClipFilter hi; hi.highlightsOnly = true; hi.limit = 12;
    section(tr("Highlights"), hi, "highlights");
    m_body->addStretch();
    connect(&Recorder::instance(), &Recorder::stateChanged, this, &HomePage::refresh);
    connect(&ClipLibrary::instance(), &ClipLibrary::changed, this, &HomePage::refresh);
    refresh();
}

void HomePage::refresh() {
    auto &r = Recorder::instance();
    if (r.bufferActive()) {
        m_state->setText(r.gameName().isEmpty() ? tr("Instant replay is on") : tr("Recording %1").arg(r.gameName()));
        m_detail->setText(tr("%1 · keeping the last %2 · %3").arg(r.captureMethod(), media::formatDuration(r.clipSeconds()), r.encoderName()));
    } else if (r.bufferPausedByUser()) {
        m_state->setText(tr("Instant replay is paused"));
        m_detail->setText(tr("Turn it back on from the top bar or the tray icon."));
    } else {
        m_state->setText(tr("Waiting for a game"));
        m_detail->setText(r.lastError().isEmpty() ? tr("Start any game and KlipIt begins recording by itself.") : r.lastError());
    }
    m_hotkeys->setText(tr("Save clip: %1   ·   Record: %2   ·   Screenshot: %3")
                           .arg(keyText("saveClip"), keyText("toggleRecording"), keyText("screenshot")));
    int clips = 0, sessions = 0, shots = 0;
    for (const auto &c : ClipLibrary::instance().items()) {
        if (c.kind == "screenshot") ++shots; else if (c.kind == "recording") ++sessions; else ++clips;
    }
    m_clips->setText(QString::number(clips));
    m_sessions->setText(QString::number(sessions));
    m_shots->setText(QString::number(shots));
    m_storage->setText(QLocale().formattedDataSize(ClipLibrary::instance().totalBytes(), 1, QLocale::DataSizeTraditionalFormat));
}

// ------------------------------------------------------------------ Library
LibraryPage::LibraryPage(const QString &title, const QString &sub, const ClipFilter &base, bool kindChips, QWidget *parent)
    : Page(title, sub, false, parent), m_base(base) {
    auto *bar = new QHBoxLayout;
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search clips and games"));
    m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(320);
    bar->addWidget(m_search);
    m_game = new QComboBox;
    m_game->setMinimumWidth(180);
    bar->addWidget(m_game);
    if (kindChips) {
        auto *group = new QButtonGroup(this);
        const QList<QPair<QString, QString>> kinds = {{tr("All"), ""}, {tr("Clips"), "clip"}, {tr("Trims"), "trim"},
                                                      {tr("Sessions"), "recording"}, {tr("Screenshots"), "screenshot"}};
        for (const auto &[label, k] : kinds) {
            auto *b = new QPushButton(label);
            b->setObjectName("Chip");
            b->setCheckable(true);
            b->setChecked(k.isEmpty());
            group->addButton(b);
            bar->addWidget(b);
            connect(b, &QPushButton::clicked, this, [this, k = k] { m_kind = k; apply(); });
        }
    }
    bar->addStretch();
    m_count = new QLabel;
    m_count->setObjectName("Muted");
    bar->addWidget(m_count);
    auto *folder = new QPushButton(tr("Open clips folder"));
    connect(folder, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(ClipLibrary::instance().root())); });
    bar->addWidget(folder);
    m_body->addLayout(bar);
    m_grid = new ClipGrid;
    connect(m_grid, &ClipGrid::openRequested, this, &Page::openClip);
    m_body->addWidget(m_grid, 1);
    connect(m_search, &QLineEdit::textChanged, this, &LibraryPage::apply);
    connect(m_game, &QComboBox::activated, this, &LibraryPage::apply);
    auto fillGames = [this] {
        const QString cur = m_game->currentData().toString();
        m_game->clear();
        m_game->addItem(tr("All games"), QString());
        for (const auto &g : ClipLibrary::instance().games()) m_game->addItem(g, g);
        const int i = m_game->findData(cur);
        m_game->setCurrentIndex(i < 0 ? 0 : i);
        apply();
    };
    connect(&ClipLibrary::instance(), &ClipLibrary::changed, this, fillGames);
    fillGames();
}

void LibraryPage::apply() {
    ClipFilter f = m_base;
    f.search = m_search->text();
    f.game = m_game->currentData().toString();
    if (!m_kind.isEmpty()) f.kinds = {m_kind};
    m_grid->setFilter(f);
    const int n = m_grid->clipModel()->rowCount();
    m_count->setText(n == 1 ? tr("1 item") : tr("%1 items").arg(n));
}

HighlightsPage::HighlightsPage(QWidget *parent)
    : LibraryPage(tr("Highlights"),
                  tr("Moments KlipIt saved by itself when you got loud in a game, so you don't miss the big plays even without pressing the hotkey."),
                  [] { ClipFilter f; f.highlightsOnly = true; return f; }(), false, parent) {
    QVBoxLayout *l;
    QWidget *c = card(&l, tr("Automatic highlights"));
    l->addWidget(check(tr("Save a highlight when I get loud (uses your microphone level only; your voice is recorded only if Audio includes voice)"),
                       "highlights.auto", true));
    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("How loud counts as a highlight")));
    row->addWidget(combo("highlights.thresholdDb", {{tr("Only shouting (least)"), -4}, {tr("Excited (default)"), -9}, {tr("Raised voice (most)"), -14}}, true));
    row->addStretch();
    l->addLayout(row);
    l->addWidget(hint(tr("At most one automatic highlight every 45 seconds. Highlights use your Instant Replay length.")));
    m_body->insertWidget(2, c);
}

// ------------------------------------------------------------------ Games
GamesPage::GamesPage(QWidget *parent)
    : Page(tr("Games"), tr("Every game KlipIt has seen. Give a game its own capture profile, or turn recording off for it."), false, parent) {
    auto *row = new QHBoxLayout;
    m_list = new QListWidget;
    m_list->setFixedWidth(280);
    m_list->setObjectName("Card");
    row->addWidget(m_list);
    m_editor = new QWidget;
    new QVBoxLayout(m_editor);
    row->addWidget(m_editor, 1);
    m_body->addLayout(row, 1);
    auto *add = new QPushButton(tr("Add a game KlipIt doesn't detect..."));
    connect(add, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Choose the game's program file"), {}, tr("Programs (*.exe)"));
        if (f.isEmpty()) return;
        const QString exe = QFileInfo(f).fileName();
        QStringList inc = S().list("games.include");
        if (!inc.contains(exe, Qt::CaseInsensitive)) inc << exe;
        S().set("games.include", inc);
        S().set(Recorder::profileKey(exe) + ".exe", exe);
        S().set(Recorder::profileKey(exe) + ".name", GameDetector::friendlyName(exe, {}));
        S().save();
        refresh();
    });
    m_body->addWidget(add, 0, Qt::AlignLeft);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int r) {
        if (r >= 0) showProfile(m_list->item(r)->data(Qt::UserRole).toString());
    });
    connect(&Recorder::instance(), &Recorder::stateChanged, this, [this] { if (m_list->count() == 0) refresh(); });
    refresh();
}

void GamesPage::refresh() {
    const QString cur = m_key;
    m_list->clear();
    const QVariantMap profiles = S().get("profiles").toMap();
    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
        const QVariantMap p = it.value().toMap();
        const QString name = p.value("name").toString().isEmpty() ? p.value("exe").toString() : p.value("name").toString();
        const int n = ClipLibrary::instance().countForGame(name);
        auto *item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(name, n == 1 ? tr("1 clip") : tr("%1 clips").arg(n)));
        item->setData(Qt::UserRole, "profiles." + it.key());
        item->setSizeHint(QSize(260, 48));
        m_list->addItem(item);
        if ("profiles." + it.key() == cur) m_list->setCurrentItem(item);
    }
    if (m_list->count() == 0) {
        auto *item = new QListWidgetItem(tr("No games yet.\nStart a game and it shows up here."));
        item->setFlags(Qt::NoItemFlags);
        m_list->addItem(item);
    } else if (!m_list->currentItem()) {
        m_list->setCurrentRow(0);
    }
}

void GamesPage::showProfile(const QString &key) {
    m_key = key;
    delete m_editor->layout();
    qDeleteAll(m_editor->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly));
    auto *l = new QVBoxLayout(m_editor);
    l->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout *c;
    const QString name = S().s(key + ".name");
    l->addWidget(card(&c, name, tr("Program: %1").arg(S().s(key + ".exe"))));
    auto *enabled = new QCheckBox(tr("Record this game"));
    enabled->setChecked(S().get(key + ".enabled").isNull() || S().b(key + ".enabled"));
    connect(enabled, &QCheckBox::toggled, this, [key](bool on) { S().set(key + ".enabled", on); saveAndApply(false); });
    c->addWidget(enabled);
    auto *form = new QFormLayout;
    form->setSpacing(10);
    auto profileCombo = [&](const QString &field, const QList<QPair<QString, QVariant>> &opts) {
        QList<QPair<QString, QVariant>> all = {{tr("Same as everywhere"), "default"}};
        all += opts;
        auto *cb = new QComboBox;
        for (const auto &[label, v] : all) cb->addItem(label, v);
        const QVariant cur = S().get(key + "." + field);
        const int i = cur.isNull() ? 0 : cb->findData(cur);
        cb->setCurrentIndex(i < 0 ? 0 : i);
        connect(cb, &QComboBox::activated, this, [cb, key, field](int idx) { S().set(key + "." + field, cb->itemData(idx)); saveAndApply(true); });
        return cb;
    };
    form->addRow(tr("Instant replay length"), profileCombo("clipSeconds", {{"15 s", 15}, {"30 s", 30}, {"45 s", 45}, {"1 min", 60}, {"2 min", 120},
                                                                           {"3 min", 180}, {"5 min", 300}, {"10 min", 600}, {"15 min", 900}, {"20 min", 1200}}));
    form->addRow(tr("Quality"), profileCombo("quality", {{tr("Low"), "low"}, {tr("Medium"), "medium"}, {tr("High"), "high"}, {tr("Ultra"), "ultra"}}));
    form->addRow(tr("Frame rate"), profileCombo("fps", {{"30 fps", 30}, {"60 fps", 60}, {"120 fps", 120}, {"144 fps", 144}}));
    form->addRow(tr("Audio"), profileCombo("audioMode", {{tr("Game only"), "game"}, {tr("Voice only"), "voice"}, {tr("Game + voice"), "both"}, {tr("No audio"), "off"}}));
    form->addRow(tr("Record whole sessions"), profileCombo("sessionRecording", {{tr("Yes"), true}, {tr("No"), false}}));
    c->addLayout(form);
    auto *forget = new QPushButton(tr("Never treat this program as a game"));
    connect(forget, &QPushButton::clicked, this, [this, key] {
        const QString exe = S().s(key + ".exe");
        QStringList ex = S().list("games.exclude");
        if (!ex.contains(exe, Qt::CaseInsensitive)) ex << exe;
        S().set("games.exclude", ex);
        QVariantMap profiles = S().get("profiles").toMap();
        profiles.remove(key.mid(QStringLiteral("profiles.").size()));
        S().set("profiles", profiles);
        S().save();
        m_key.clear();
        refresh();
    });
    c->addWidget(forget, 0, Qt::AlignLeft);
    l->addStretch();
}

// ------------------------------------------------------------------ Capture
CapturePage::CapturePage(QWidget *parent) : Page(tr("Capture"), tr("How KlipIt records. GPU encoding keeps your game's frame rate up."), true, parent) {
    QVBoxLayout *c;
    m_body->addWidget(card(&c, tr("Monitor"), tr("Pick the monitor KlipIt records. In game mode the game itself is always captured, so this sets the video size "
                                               "and the screen recorded if a game blocks game capture. In screen mode, this is the screen that's recorded.")));
    c->addWidget(new MonitorPicker);

    m_body->addWidget(card(&c, tr("What to record")));
    auto *mode = combo("capture.mode", {{tr("Games, automatically (recommended)"), "game"}, {tr("My whole screen, all the time"), "screen"}}, true);
    c->addWidget(mode);
    c->addWidget(hint(tr("Game mode hooks straight into the game, like Medal and SteelSeries Moments: it records only the game, "
                         "never Discord, browsers or other windows. If a game's anti-cheat blocks that, KlipIt records the game's monitor instead.")));
    c->addWidget(check(tr("Only keep instant replay running while a game is open"), "capture.onlyWhenGame", true));
    c->addWidget(check(tr("Show the mouse pointer in recordings"), "capture.cursor", true));

    m_body->addWidget(card(&c, tr("Video")));
    auto *form = new QFormLayout;
    form->setSpacing(10);
    form->addRow(tr("Resolution"), combo("capture.resolution", {{tr("Up to 1440p (2K)"), "1440"}, {tr("Up to 1080p"), "1080"},
                                                                {tr("Up to 720p"), "720"}, {tr("Native (your monitor's size)"), "native"}}, true));
    form->addRow(tr("Frame rate"), combo("capture.fps", {{"30 fps", 30}, {"60 fps", 60}, {"120 fps", 120}, {"144 fps", 144}}, true));
    form->addRow(tr("Quality"), combo("capture.quality", {{tr("Low (smallest files)"), "low"}, {tr("Medium"), "medium"}, {tr("High"), "high"}, {tr("Ultra"), "ultra"}}, true));
    QList<QPair<QString, QVariant>> encs = {{tr("Automatic (best GPU encoder)"), "auto"}};
    for (const auto &e : Recorder::instance().encoders())
        encs << qMakePair(e.name + (e.hardware ? tr("  (GPU)") : tr("  (CPU)")), QVariant(e.id));
    form->addRow(tr("Encoder"), combo("capture.encoder", encs, true));
    form->addRow(tr("Format"), combo("capture.codec", {{tr("H.264 (plays everywhere)"), "h264"}, {tr("HEVC (smaller files)"), "hevc"}}, true));
    c->addLayout(form);
    m_estimate = hint({});
    c->addWidget(m_estimate);

    m_body->addWidget(card(&c, tr("Full-session recording")));
    c->addWidget(check(tr("Record every game session from start to finish"), "capture.sessionRecording", false));
    c->addWidget(hint(tr("Long recordings are saved as consecutive parts of up to 20 minutes each, so nothing is cut off and nothing is lost. "
                         "You can also start and stop a recording any time with %1.").arg(keyText("toggleRecording"))));
    m_body->addStretch();
    connect(&Recorder::instance(), &Recorder::stateChanged, this, &CapturePage::refresh);
    connect(&S(), &Settings::changed, this, &CapturePage::refresh);
    refresh();
}

void CapturePage::refresh() {
    const QSize c = Recorder::instance().canvasSize();
    const int kbps = Recorder::bitrateKbps(c.height() > 0 ? c.height() : 1440, S().i("capture.fps"), S().s("capture.quality"));
    m_estimate->setText(tr("About %1 Mbps, roughly %2 per minute of video. Recording size now: %3.")
                            .arg(kbps / 1000.0, 0, 'f', 1)
                            .arg(QLocale().formattedDataSize(qint64(kbps) * 1000 / 8 * 60, 0, QLocale::DataSizeTraditionalFormat))
                            .arg(c.isValid() ? QStringLiteral("%1×%2").arg(c.width()).arg(c.height()) : tr("starts with the first game")));
}

// ------------------------------------------------------------------ Instant replay
static const QList<QPair<QString, int>> kPresets = {{"15 s", 15}, {"30 s", 30}, {"45 s", 45}, {"1 min", 60}, {"2 min", 120},
                                                    {"3 min", 180}, {"5 min", 300}, {"10 min", 600}, {"15 min", 900}, {"20 min", 1200}};

ReplayPage::ReplayPage(QWidget *parent)
    : Page(tr("Instant Replay"), tr("KlipIt quietly keeps the last moments of your game. Press the hotkey and they're saved as a clip."), true, parent) {
    QVBoxLayout *c;
    m_body->addWidget(card(&c, tr("Status")));
    auto *srow = new QHBoxLayout;
    m_state = new QLabel;
    srow->addWidget(m_state, 1);
    m_toggle = new QPushButton;
    connect(m_toggle, &QPushButton::clicked, this, [] { Recorder::instance().setBufferPaused(!Recorder::instance().bufferPausedByUser()); });
    srow->addWidget(m_toggle);
    c->addLayout(srow);

    m_body->addWidget(card(&c, tr("Monitor"), tr("Which monitor KlipIt records. Automatic follows your game.")));
    c->addWidget(new MonitorPicker);

    m_body->addWidget(card(&c, tr("Clip length"), tr("How much of what just happened gets saved when you press the hotkey. Clips can be up to 20 minutes.")));
    auto *grid = new QGridLayout;
    auto *group = new QButtonGroup(this);
    for (int i = 0; i < kPresets.size(); ++i) {
        auto *b = new QPushButton(kPresets[i].first);
        b->setObjectName("Chip");
        b->setCheckable(true);
        group->addButton(b);
        grid->addWidget(b, i / 5, i % 5);
        connect(b, &QPushButton::clicked, this, [this, s = kPresets[i].second] { setLength(s); });
        m_presets << b;
    }
    c->addLayout(grid);
    auto *custom = new QHBoxLayout;
    custom->addWidget(new QLabel(tr("Custom length")));
    m_custom = new QSpinBox;
    m_custom->setRange(1, 1200);
    m_custom->setSuffix(tr(" seconds"));
    m_custom->setValue(S().i("capture.clipSeconds"));
    custom->addWidget(m_custom);
    custom->addWidget(hint(tr("1 second to 20 minutes")));
    custom->addStretch();
    c->addLayout(custom);
    connect(m_custom, &QSpinBox::editingFinished, this, [this] { setLength(m_custom->value()); });

    m_body->addWidget(card(&c, tr("Before and after"), tr("Pre-event is how far back the clip reaches. Post-event keeps recording a few seconds after you press "
                                                          "the hotkey, so the reaction is in the clip too.")));
    auto *pp = new QFormLayout;
    auto *pre = new QLabel;
    pp->addRow(tr("Pre-event (before the hotkey)"), pre);
    m_post = new QSpinBox;
    m_post->setRange(0, 300);
    m_post->setSuffix(tr(" seconds"));
    m_post->setValue(S().i("capture.postSeconds"));
    pp->addRow(tr("Post-event (after the hotkey)"), m_post);
    c->addLayout(pp);
    m_total = new QLabel;
    c->addWidget(m_total);
    connect(m_post, &QSpinBox::editingFinished, this, [this] {
        const int pre = S().i("capture.clipSeconds");
        int post = m_post->value();
        if (pre + post > 1200) {
            post = 1200 - pre;
            m_post->setValue(post);
            QMessageBox::information(this, tr("20-minute limit"), tr("A clip can be at most 20 minutes, so post-event was set to %1 seconds.").arg(post));
        }
        S().set("capture.postSeconds", post);
        saveAndApply(true);
        refresh();
    });
    pre->setObjectName("PreLabel");

    m_body->addWidget(card(&c, tr("Save hotkey, audio and location")));
    auto *form = new QFormLayout;
    form->setSpacing(10);
    auto *msg = new QLabel;
    msg->setObjectName("Hint");
    form->addRow(tr("Save clip"), new HotkeyEdit("saveClip", msg));
    form->addRow(QString(), msg);
    form->addRow(tr("Audio tracks"), combo("audio.mode", {{tr("Game only"), "game"}, {tr("Voice only (microphone)"), "voice"},
                                                         {tr("Game + voice (3 tracks: mixed, game, voice)"), "both"}, {tr("No audio"), "off"}}, true));
    auto *loc = new QHBoxLayout;
    auto *folder = new QLineEdit(QDir::toNativeSeparators(S().s("clips.folder")));
    folder->setReadOnly(true);
    auto *browse = new QPushButton(tr("Change..."));
    connect(browse, &QPushButton::clicked, this, [this, folder] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Save clips in"), S().s("clips.folder"));
        if (d.isEmpty()) return;
        S().set("clips.folder", d);
        S().save();
        folder->setText(QDir::toNativeSeparators(d));
        ClipLibrary::instance().setRoot(d);
        Recorder::instance().applySettings();
    });
    loc->addWidget(folder, 1);
    loc->addWidget(browse);
    form->addRow(tr("Save location"), loc);
    c->addLayout(form);
    c->addWidget(hint(tr("Only the game's own sound is recorded, never Discord, YouTube, music or other apps. Recording quality is under Capture.")));
    m_ram = hint({});
    c->addWidget(m_ram);
    m_body->addStretch();
    connect(&Recorder::instance(), &Recorder::stateChanged, this, &ReplayPage::refresh);
    refresh();
}

void ReplayPage::setLength(int seconds) {
    seconds = qBound(1, seconds, 1200);
    const int post = S().i("capture.postSeconds");
    if (seconds + post > 1200) {
        QMessageBox::information(this, tr("20-minute limit"), tr("With %1 seconds of post-event, the longest pre-event is %2.")
                                                                   .arg(post).arg(media::formatDuration(1200 - post)));
        seconds = 1200 - post;
    }
    S().set("capture.clipSeconds", seconds);
    saveAndApply(true);
    refresh();
}

void ReplayPage::refresh() {
    auto &r = Recorder::instance();
    const int pre = S().i("capture.clipSeconds"), post = S().i("capture.postSeconds");
    for (int i = 0; i < m_presets.size(); ++i) m_presets[i]->setChecked(kPresets[i].second == pre);
    m_custom->setValue(pre);
    if (auto *l = findChild<QLabel *>("PreLabel")) l->setText(media::formatDuration(pre));
    m_total->setText(tr("Each saved clip: %1").arg(media::formatDuration(pre + post)));
    m_state->setText(r.bufferActive() ? tr("On · %1 · %2").arg(r.gameName().isEmpty() ? tr("screen") : r.gameName(), r.captureMethod())
                     : r.bufferPausedByUser() ? tr("Paused by you") : tr("Off until a game starts"));
    m_toggle->setText(r.bufferPausedByUser() ? tr("Turn instant replay on") : tr("Pause instant replay"));
    const double mb = r.estimatedBufferMB();
    m_ram->setText(tr("Instant replay keeps about %1 of video in memory for this length.")
                       .arg(QLocale().formattedDataSize(qint64(mb * 1024 * 1024), 1, QLocale::DataSizeTraditionalFormat)) +
                   (mb > 2048 ? tr(" That's a lot; if your PC has 16 GB of memory or less, a shorter length or lower quality is kinder to your games.") : QString()));
    m_ram->setObjectName(mb > 2048 ? "Warn" : "Hint");
    repolish(m_ram);
}

// ------------------------------------------------------------------ Settings
class ThemeSwatch : public QRadioButton {
public:
    ThemeSwatch(const theme::Palette &p) : QRadioButton(p.name), m_p(p) { setMinimumSize(150, 96); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter g(this);
        g.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
        g.setPen(QPen(isChecked() ? theme::current().accent : QColor(184, 178, 191, 80), isChecked() ? 2 : 1));
        g.setBrush(m_p.bg);
        g.drawRoundedRect(r, 10, 10);
        g.setPen(Qt::NoPen);
        g.setBrush(m_p.panel);
        g.drawRoundedRect(QRectF(r.left() + 8, r.top() + 8, 34, r.height() - 36), 5, 5);
        g.setBrush(m_p.raised);
        g.drawRoundedRect(QRectF(r.left() + 48, r.top() + 8, r.width() - 56, 18), 5, 5);
        g.setBrush(m_p.accent);
        g.drawRoundedRect(QRectF(r.left() + 48, r.top() + 32, 46, 14), 5, 5);
        g.setBrush(m_p.silver);
        g.drawRoundedRect(QRectF(r.left() + 100, r.top() + 32, 28, 14), 5, 5);
        g.setPen(m_p.text);
        QFont f(theme::uiFamily(), 9, isChecked() ? QFont::Bold : QFont::Normal);
        g.setFont(f);
        g.drawText(QRectF(r.left() + 10, r.bottom() - 24, r.width() - 20, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   m_p.name + (m_p.id == "black" ? tr("  (default)") : QString()));
    }
private:
    theme::Palette m_p;
};

// ------------------------------------------------------------------ Account
AccountPage::AccountPage(QWidget *parent)
    : Page(tr("Account"), tr("Sign in with your KlipIt website account and the clips you save show up in My clips on the website, "
                             "private to you."), true, parent) {
    QVBoxLayout *c;
    m_body->addWidget(card(&c, tr("KlipIt account")));
    m_who = new QLabel;
    m_who->setObjectName("AccountName");
    m_who->setWordWrap(true);
    m_detail = hint({});
    m_error = new QLabel;
    m_error->setObjectName("Warn");
    m_error->setWordWrap(true);
    c->addWidget(m_who);
    c->addWidget(m_detail);
    c->addWidget(m_error);
    auto *row = new QHBoxLayout;
    m_signIn = primary(tr("Sign in"));
    m_create = new QPushButton(tr("Create an account"));
    m_cancel = new QPushButton(tr("Cancel"));
    m_open = primary(tr("Open My clips"));
    m_signOut = new QPushButton(tr("Sign out"));
    for (QPushButton *b : {m_signIn, m_create, m_cancel, m_open, m_signOut}) row->addWidget(b);
    row->addStretch();
    c->addLayout(row);
    connect(m_signIn, &QPushButton::clicked, this, [] { Account::instance().signIn(false); });
    connect(m_create, &QPushButton::clicked, this, [] { Account::instance().signIn(true); });
    connect(m_cancel, &QPushButton::clicked, this, [] { Account::instance().cancelSignIn(); });
    connect(m_open, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(Account::siteUrl() + "/clips.html")); });
    connect(m_signOut, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Sign out"), tr("Sign out of your KlipIt account?\n\nClips already on your website stay there.")) ==
            QMessageBox::Yes)
            Account::instance().signOut();
    });

    m_body->addWidget(card(&c, tr("Sending clips"), tr("Clips go to My clips on the website. Only you can see them there.")));
    m_auto = check(tr("Send new clips to my website automatically"), "cloud.autoSend", false);
    c->addWidget(m_auto);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    c->addWidget(m_status);
    c->addWidget(hint(tr("To send an older clip, right-click it in your library and choose Send to my website. Each video can be up to "
                         "4 GB; your Wix plan sets how much space the website has in total.")));
    m_body->addStretch();

    auto &up = WebUploader::instance();
    connect(&Account::instance(), &Account::changed, this, &AccountPage::refresh);
    connect(&up, &WebUploader::queueChanged, this, &AccountPage::refresh);
    connect(&up, &WebUploader::progress, this, [this](const QString &, int pct) { m_percent = pct; refresh(); });
    refresh();
}

void AccountPage::refresh() {
    const auto &a = Account::instance();
    const bool in = a.signedIn(), busy = a.signingIn();
    if (in) {
        m_who->setText(tr("Signed in as %1").arg(a.name()));
        m_detail->setText(a.email());
    } else if (busy) {
        m_who->setText(tr("Finish signing in in your browser"));
        m_detail->setText(tr("Your browser opened the KlipIt website. Log in there and KlipIt signs in by itself."));
    } else {
        m_who->setText(tr("You're not signed in"));
        m_detail->setText(tr("Use the account from the KlipIt website, or create one. It's free."));
    }
    m_error->setText(a.lastError());
    m_error->setVisible(!in && !busy && !a.lastError().isEmpty());
    m_signIn->setVisible(!in && !busy);
    m_create->setVisible(!in && !busy);
    m_cancel->setVisible(busy);
    m_open->setVisible(in);
    m_signOut->setVisible(in);
    m_auto->setEnabled(in);

    const auto &up = WebUploader::instance();
    QString status;
    if (!in) status = tr("Sign in to send clips.");
    else if (up.isBusy()) {
        status = tr("Sending “%1” · %2%").arg(QFileInfo(up.current()).completeBaseName()).arg(m_percent);
        if (up.pending() > 1) status += tr("  (%n more waiting)", nullptr, up.pending() - 1);
    } else status = tr("Nothing waiting to send.");
    m_status->setText(status);
}

SettingsPage::SettingsPage(QWidget *parent) : Page(tr("Settings"), {}, true, parent) {
    QVBoxLayout *c;
    m_body->addWidget(card(&c, tr("General")));
    auto *autostart = new QCheckBox(tr("Start KlipIt when Windows starts (it waits quietly in the tray)"));
    autostart->setChecked(autostart::isEnabled() || S().b("general.startWithWindows"));
    connect(autostart, &QCheckBox::toggled, this, [](bool on) { autostart::setEnabled(on); S().set("general.startWithWindows", on); S().save(); });
    c->addWidget(autostart);
    c->addWidget(check(tr("Keep KlipIt running in the tray when I close the window"), "general.closeToTray", false));
    c->addWidget(check(tr("Show a pop-up when a clip or screenshot is saved"), "general.notifications", false));
    c->addWidget(check(tr("Play a sound when a clip is saved"), "general.clipSound", false));
    c->addWidget(check(tr("Animated splash screen and decorations"), "general.animations", false));

    m_body->addWidget(card(&c, tr("Theme")));
    auto *row = new QHBoxLayout;
    auto *group = new QButtonGroup(this);
    for (const auto &p : theme::palettes()) {
        auto *sw = new ThemeSwatch(p);
        sw->setChecked(S().s("appearance.theme") == p.id);
        group->addButton(sw);
        row->addWidget(sw);
        connect(sw, &QRadioButton::toggled, this, [id = p.id](bool on) {
            if (!on) return;
            S().set("appearance.theme", id);
            S().save();
            theme::apply(*qApp, id);
            for (QWidget *w : QApplication::allWidgets()) w->update();
        });
    }
    row->addStretch();
    c->addLayout(row);

    m_body->addWidget(card(&c, tr("Hotkeys"), tr("These work everywhere, even inside full-screen games. Click a box and press any key, numpad keys like Num * included.")));
    auto *form = new QFormLayout;
    form->setSpacing(10);
    auto *msg = new QLabel;
    msg->setObjectName("Hint");
    for (const auto &a : HotkeyManager::instance().actions()) form->addRow(a.label, new HotkeyEdit(a.id, msg));
    form->addRow(QString(), msg);
    c->addLayout(form);

    m_body->addWidget(card(&c, tr("Storage")));
    auto *used = new QLabel;
    auto updateUsed = [used] {
        used->setText(tr("%1 in %2").arg(QLocale().formattedDataSize(ClipLibrary::instance().totalBytes(), 1, QLocale::DataSizeTraditionalFormat),
                                         QDir::toNativeSeparators(ClipLibrary::instance().root())));
    };
    updateUsed();
    connect(&ClipLibrary::instance(), &ClipLibrary::changed, used, updateUsed);
    c->addWidget(used);
    auto *open = new QPushButton(tr("Open clips folder"));
    connect(open, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(ClipLibrary::instance().root())); });
    c->addWidget(open, 0, Qt::AlignLeft);

    m_body->addWidget(card(&c, tr("About KlipIt")));
    c->addWidget(new QLabel(tr("KlipIt %1").arg(KL_APP_VERSION)));
    c->addWidget(hint(tr("Recording is powered by the open-source capture engine from OBS Studio (GPL-2.0), used only for capture and encoding. KlipIt never streams.")));
    auto *logs = new QPushButton(tr("Open log folder"));
    connect(logs, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::logs())); });
    c->addWidget(logs, 0, Qt::AlignLeft);
    m_body->addStretch();
}

// ------------------------------------------------------------------ Plugins
PluginsPage::PluginsPage(QWidget *parent)
    : Page(tr("Plugins"), tr("The capture parts KlipIt loads. Only capture and encoding parts are allowed in, which keeps KlipIt light and stable."), false, parent) {
    m_list = new QListWidget;
    m_list->setObjectName("Card");
    m_body->addWidget(m_list, 1);
    refresh();
}

void PluginsPage::refresh() {
    static const QHash<QString, QString> what = {
        {"win-capture", tr("Game capture and monitor capture")}, {"win-wasapi", tr("Game-only audio and microphone")},
        {"obs-nvenc", tr("NVIDIA GPU encoder")}, {"obs-qsv11", tr("Intel Quick Sync GPU encoder")},
        {"obs-ffmpeg", tr("Instant replay, recording and AMD GPU encoder")}, {"obs-x264", tr("CPU encoder (backup)")},
        {"image-source", tr("Images (used by tests)")}, {"linux-capture", tr("Screen capture (Linux)")},
        {"linux-pulseaudio", tr("Audio (Linux)")}};
    m_list->clear();
    for (const auto &m : Recorder::instance().loadedModules()) {
        const QString base = QFileInfo(m).completeBaseName();
        auto *it = new QListWidgetItem(QStringLiteral("%1\n%2").arg(base, what.value(base, tr("Engine component"))));
        it->setSizeHint(QSize(300, 46));
        m_list->addItem(it);
    }
    if (m_list->count() == 0) m_list->addItem(tr("The capture engine isn't running."));
}

} // namespace kl
