#include "ui/EditorPage.h"
#include "core/ClipLibrary.h"
#include "core/MediaTools.h"
#include "core/Paths.h"
#include "core/Theme.h"
#include "ui/ClipGrid.h"
#include "ui/Toast.h"

#include <QAudioOutput>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressDialog>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QtConcurrent>
#include <QEventLoop>
#include <QLocale>
#include <QStyle>
#include <QTimer>
#include <atomic>
#include <cmath>

namespace kl {

// ------------------------------------------------------------------ TrimBar
TrimBar::TrimBar(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(56);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}
void TrimBar::setDuration(double s) { m_dur = qMax(0.0, s); m_a = 0; m_b = qMin(m_dur, media::kMaxClipSeconds); update(); }
void TrimBar::setPosition(double s) { m_pos = s; update(); }
void TrimBar::setRange(double a, double b) { m_a = qBound(0.0, a, m_dur); m_b = qBound(m_a, b, m_dur); update(); Q_EMIT rangeChanged(m_a, m_b); }
double TrimBar::timeToX(double t) const { return 12 + (width() - 24) * (m_dur > 0 ? t / m_dur : 0); }
double TrimBar::xToTime(double x) const { return qBound(0.0, (x - 12) / qMax(1, width() - 24) * m_dur, m_dur); }

void TrimBar::paintEvent(QPaintEvent *) {
    const auto &pal = theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF track(12, 18, width() - 24, 20);
    p.setPen(QPen(QColor(pal.silver.red(), pal.silver.green(), pal.silver.blue(), 70), 1));
    p.setBrush(pal.raised);
    p.drawRoundedRect(track, 6, 6);
    if (m_dur <= 0) return;
    const double xa = timeToX(m_a), xb = timeToX(m_b);
    QColor sel = pal.accent; sel.setAlpha(m_b - m_a > media::kMaxClipSeconds + 0.01 ? 60 : 110);
    p.setPen(Qt::NoPen);
    p.setBrush(sel);
    p.drawRect(QRectF(xa, track.top(), xb - xa, track.height()));
    for (double x : {xa, xb}) {   // handles
        p.setBrush(pal.silver);
        p.drawRoundedRect(QRectF(x - 4, track.top() - 6, 8, track.height() + 12), 3, 3);
    }
    const double xp = timeToX(m_pos);   // playhead
    p.setBrush(pal.text);
    p.drawRect(QRectF(xp - 1, 8, 2, height() - 16));
    p.drawEllipse(QPointF(xp, 8), 4, 4);
}

void TrimBar::mousePressEvent(QMouseEvent *e) {
    const double x = e->position().x();
    if (std::abs(x - timeToX(m_a)) < 9) m_drag = 1;
    else if (std::abs(x - timeToX(m_b)) < 9) m_drag = 2;
    else { m_drag = 3; m_pos = xToTime(x); Q_EMIT seekRequested(m_pos); }
    update();
}
void TrimBar::mouseMoveEvent(QMouseEvent *e) {
    const double t = xToTime(e->position().x());
    if (m_drag == 1) { m_a = qMin(t, m_b - 0.1); Q_EMIT rangeChanged(m_a, m_b); Q_EMIT seekRequested(m_a); }
    else if (m_drag == 2) { m_b = qMax(t, m_a + 0.1); Q_EMIT rangeChanged(m_a, m_b); Q_EMIT seekRequested(m_b); }
    else if (m_drag == 3) { m_pos = t; Q_EMIT seekRequested(t); }
    else {
        const double x = e->position().x();
        setCursor(std::abs(x - timeToX(m_a)) < 9 || std::abs(x - timeToX(m_b)) < 9 ? Qt::SizeHorCursor : Qt::PointingHandCursor);
    }
    update();
}
void TrimBar::mouseReleaseEvent(QMouseEvent *) { m_drag = 0; }

// ------------------------------------------------------------------ EditorPage
static QString clock(double s) {
    const int ms = int(std::lround(qMax(0.0, s) * 10));
    const int t = ms / 10;
    return t >= 3600 ? QStringLiteral("%1:%2:%3.%4").arg(t / 3600).arg(t / 60 % 60, 2, 10, QChar('0')).arg(t % 60, 2, 10, QChar('0')).arg(ms % 10)
                     : QStringLiteral("%1:%2.%3").arg(t / 60).arg(t % 60, 2, 10, QChar('0')).arg(ms % 10);
}

EditorPage::EditorPage(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 22, 28, 22);
    root->setSpacing(12);
    auto *head = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    m_title = new QLabel(tr("Video Editor"));
    m_title->setObjectName("PageTitle");
    m_info = new QLabel(tr("Open a clip from your library, or import a video."));
    m_info->setObjectName("PageSub");
    titles->addWidget(m_title);
    titles->addWidget(m_info);
    head->addLayout(titles, 1);
    auto *importBtn = new QPushButton(tr("Import video..."));
    connect(importBtn, &QPushButton::clicked, this, &EditorPage::importVideo);
    head->addWidget(importBtn, 0, Qt::AlignTop);
    root->addLayout(head);

    m_view = new QStackedWidget;
    m_view->setObjectName("VideoArea");
    m_empty = new QLabel(tr("No clip open.\nDouble-click any clip in your library to edit it here."));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setObjectName("Muted");
    m_video = new QVideoWidget;
    m_image = new QLabel;
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setMinimumSize(200, 120);
    m_view->addWidget(m_empty);
    m_view->addWidget(m_video);
    m_view->addWidget(m_image);
    root->addWidget(m_view, 1);

    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_audio->setVolume(0.8f);
    m_player->setAudioOutput(m_audio);
    m_player->setVideoOutput(m_video);

    m_tools = new QWidget;
    auto *tl = new QVBoxLayout(m_tools);
    tl->setContentsMargins(0, 0, 0, 0);
    m_bar = new TrimBar;
    tl->addWidget(m_bar);
    auto *controls = new QHBoxLayout;
    m_play = new QPushButton(tr("Play"));
    auto *back = new QPushButton(tr("−5 s"));
    auto *fwd = new QPushButton(tr("+5 s"));
    auto *setIn = new QPushButton(tr("Set start  [I]"));
    auto *setOut = new QPushButton(tr("Set end  [O]"));
    m_time = new QLabel("0:00.0 / 0:00.0");
    m_time->setObjectName("Muted");
    auto *vol = new QSlider(Qt::Horizontal);
    vol->setRange(0, 100);
    vol->setValue(80);
    vol->setFixedWidth(110);
    vol->setToolTip(tr("Volume"));
    for (QWidget *w : {static_cast<QWidget *>(m_play), static_cast<QWidget *>(back), static_cast<QWidget *>(fwd)}) controls->addWidget(w);
    controls->addWidget(m_time);
    controls->addStretch();
    controls->addWidget(setIn);
    controls->addWidget(setOut);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Volume")));
    controls->addWidget(vol);
    tl->addLayout(controls);
    m_range = new QLabel;
    tl->addWidget(m_range);
    auto *actions = new QHBoxLayout;
    m_save = new QPushButton(tr("Save trim as new clip"));
    m_save->setObjectName("Primary");
    m_split = new QPushButton(tr("Split into 20-minute parts"));
    m_fav = new QPushButton(tr("☆ Favorite"));
    auto *copy = new QPushButton(tr("Copy clip"));
    copy->setToolTip(tr("Copies the video file so you can paste it into Discord, a chat or a folder."));
    auto *folder = new QPushButton(tr("Show in folder"));
    auto *ren = new QPushButton(tr("Rename"));
    auto *del = new QPushButton(tr("Delete"));
    del->setObjectName("Danger");
    actions->addWidget(m_save);
    actions->addWidget(m_split);
    actions->addStretch();
    for (QPushButton *b : {m_fav, copy, folder, ren, del}) actions->addWidget(b);
    tl->addLayout(actions);
    root->addWidget(m_tools);
    m_tools->setEnabled(false);

    connect(m_play, &QPushButton::clicked, this, [this] {
        if (m_player->playbackState() == QMediaPlayer::PlayingState) m_player->pause(); else m_player->play();
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
        m_play->setText(s == QMediaPlayer::PlayingState ? tr("Pause") : tr("Play"));
    });
    connect(back, &QPushButton::clicked, this, [this] { m_player->setPosition(qMax<qint64>(0, m_player->position() - 5000)); });
    connect(fwd, &QPushButton::clicked, this, [this] { m_player->setPosition(m_player->position() + 5000); });
    connect(vol, &QSlider::valueChanged, this, [this](int v) { m_audio->setVolume(float(v) / 100.f); });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
        m_bar->setPosition(double(ms) / 1000.0);
        m_time->setText(clock(double(ms) / 1000.0) + " / " + clock(m_duration));
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 ms) {
        if (ms > 0 && m_duration <= 0) { m_duration = double(ms) / 1000.0; m_bar->setDuration(m_duration); updateRangeLabel(); }
    });
    connect(m_bar, &TrimBar::seekRequested, this, [this](double s) { m_player->setPosition(qint64(s * 1000)); });
    connect(m_bar, &TrimBar::rangeChanged, this, &EditorPage::updateRangeLabel);
    auto markIn = [this] { m_bar->setRange(double(m_player->position()) / 1000.0, m_bar->end()); };
    auto markOut = [this] { m_bar->setRange(m_bar->start(), double(m_player->position()) / 1000.0); };
    connect(setIn, &QPushButton::clicked, this, markIn);
    connect(setOut, &QPushButton::clicked, this, markOut);
    auto *si = new QShortcut(QKeySequence(Qt::Key_I), this); connect(si, &QShortcut::activated, this, markIn);
    auto *so = new QShortcut(QKeySequence(Qt::Key_O), this); connect(so, &QShortcut::activated, this, markOut);
    auto *sp = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(sp, &QShortcut::activated, m_play, &QPushButton::click);
    connect(m_save, &QPushButton::clicked, this, &EditorPage::saveTrim);
    connect(m_split, &QPushButton::clicked, this, &EditorPage::splitParts);
    connect(m_fav, &QPushButton::clicked, this, [this] {
        if (const ClipItem *c = ClipLibrary::instance().find(m_path)) ClipLibrary::instance().setFavorite(m_path, !c->favorite);
        refreshInfo();
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        clipactions::copyToClipboard(m_path);
        Toast::show(tr("Copied"), tr("Paste it into Discord, a chat or a folder."));
    });
    connect(folder, &QPushButton::clicked, this, [this] { clipactions::showInFolder(m_path); });
    connect(ren, &QPushButton::clicked, this, [this] { if (clipactions::rename(this, m_path)) refreshInfo(); });
    connect(del, &QPushButton::clicked, this, [this] {
        m_player->stop();
        m_player->setSource({});
        if (clipactions::remove(this, m_path)) { m_path.clear(); m_view->setCurrentWidget(m_empty); m_tools->setEnabled(false); m_title->setText(tr("Video Editor")); }
        else open(m_path);
    });
}

void EditorPage::open(const QString &path) {
    if (path.isEmpty() || !QFileInfo::exists(path)) return;
    m_player->stop();
    m_path = path;
    m_duration = 0;
    const ClipItem *c = ClipLibrary::instance().find(path);
    const bool image = c ? !c->isVideo() : QFileInfo(path).suffix().compare("png", Qt::CaseInsensitive) == 0;
    m_tools->setEnabled(true);
    m_bar->setVisible(!image);
    m_save->setVisible(!image);
    m_split->setVisible(false);
    m_play->setEnabled(!image);
    if (image) {
        m_player->setSource({});
        const QPixmap px(path);
        m_image->setPixmap(px.scaled(QSize(1280, 720).boundedTo(m_view->size()), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_view->setCurrentWidget(m_image);
    } else {
        m_view->setCurrentWidget(m_video);
        const media::Info info = media::probe(path);
        m_duration = info.duration;
        m_bar->setDuration(m_duration);
        m_split->setVisible(m_duration > media::kMaxClipSeconds);
        m_player->setSource(QUrl::fromLocalFile(path));
        m_player->play();
    }
    refreshInfo();
    updateRangeLabel();
}

void EditorPage::refreshInfo() {
    const ClipItem *c = ClipLibrary::instance().find(m_path);
    m_title->setText(c ? c->displayTitle() : QFileInfo(m_path).completeBaseName());
    const media::Info info = c && c->isVideo() ? media::probe(m_path) : media::Info{};
    QStringList bits;
    if (c) bits << c->game;
    if (info.ok) bits << media::formatDuration(info.duration) << QStringLiteral("%1×%2").arg(info.width).arg(info.height)
                      << QStringLiteral("%1 fps").arg(qRound(info.fps));
    if (c) bits << QLocale().toString(c->created, QLocale::LongFormat);
    m_info->setText(bits.join("  ·  "));
    m_fav->setText(c && c->favorite ? tr("★ Favorite") : tr("☆ Favorite"));
}

void EditorPage::updateRangeLabel() {
    if (m_duration <= 0) { m_range->clear(); return; }
    const double len = m_bar->end() - m_bar->start();
    const bool tooLong = len > media::kMaxClipSeconds + 0.01;
    m_range->setObjectName(tooLong ? "Warn" : "Hint");
    m_range->setText(tooLong ? tr("The selection is %1. A clip can be at most 20 minutes, so move the start or end closer together.").arg(media::formatDuration(len))
                             : tr("Start %1   ·   End %2   ·   New clip length %3   (starts on the nearest keyframe, at most a second earlier)")
                                   .arg(clock(m_bar->start()), clock(m_bar->end()), clock(len)));
    m_range->style()->unpolish(m_range);
    m_range->style()->polish(m_range);
    m_save->setEnabled(!tooLong && len > 0.2);
}

void EditorPage::saveTrim() {
    const double a = m_bar->start(), b = m_bar->end();
    if (b - a > media::kMaxClipSeconds + 0.01) {
        QMessageBox::warning(this, tr("Too long"), tr("A clip can be at most 20 minutes long."));
        return;
    }
    const ClipItem *c = ClipLibrary::instance().find(m_path);
    const QString game = c ? c->game : QStringLiteral("Imported");
    const QString base = (c ? c->displayTitle() : QFileInfo(m_path).completeBaseName());
    const QString dir = ClipLibrary::instance().folderFor(game, "clip");
    QString dst = dir + "/" + safeFileName(base + " (trim)") + ".mp4";
    for (int n = 2; QFile::exists(dst); ++n) dst = dir + "/" + safeFileName(base + QStringLiteral(" (trim %1)").arg(n)) + ".mp4";
    const QString src = m_path;
    const bool highlight = c && c->highlight;
    QProgressDialog dlg(tr("Saving your trimmed clip..."), tr("Cancel"), 0, 100, this);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(300);
    std::atomic<int> progress{0};
    std::atomic<bool> cancel{false};
    QString err;
    QFutureWatcher<bool> w;
    QEventLoop loop;
    connect(&w, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
    connect(&dlg, &QProgressDialog::canceled, this, [&] { cancel = true; });
    QTimer tick;
    connect(&tick, &QTimer::timeout, this, [&] { dlg.setValue(progress); });
    tick.start(50);
    w.setFuture(QtConcurrent::run([&] {
        return media::cut(src, dst, a, b, &err, [&](double f) { progress = int(f * 100); return !cancel.load(); });
    }));
    loop.exec();
    tick.stop();
    dlg.close();
    if (!w.result()) {
        if (!cancel) QMessageBox::warning(this, tr("Couldn't save the trim"), err);
        return;
    }
    ClipLibrary::instance().add(dst, game, "trim", highlight, base + tr(" (trim)"));
    Toast::show(tr("Trim saved"), tr("Saved as a new clip. The original is still there."));
    Q_EMIT openClip(dst);
}

void EditorPage::splitParts() {
    const ClipItem *c = ClipLibrary::instance().find(m_path);
    const QString game = c ? c->game : QStringLiteral("Imported");
    const QString base = safeFileName(c ? c->displayTitle() : QFileInfo(m_path).completeBaseName());
    const int parts = int(std::ceil(m_duration / media::kSplitSeconds));
    if (QMessageBox::question(this, tr("Split video"), tr("Split this %1 video into %2 parts of up to 20 minutes each? The original is kept.")
                                  .arg(media::formatDuration(m_duration)).arg(parts)) != QMessageBox::Yes)
        return;
    QProgressDialog dlg(tr("Splitting..."), tr("Cancel"), 0, 100, this);
    dlg.setWindowModality(Qt::WindowModal);
    std::atomic<int> progress{0};
    std::atomic<bool> cancel{false};
    QString err;
    const QString src = m_path, dir = ClipLibrary::instance().folderFor(game, c && c->kind == "recording" ? "recording" : "clip");
    QFutureWatcher<QStringList> w;
    QEventLoop loop;
    connect(&w, &QFutureWatcher<QStringList>::finished, &loop, &QEventLoop::quit);
    connect(&dlg, &QProgressDialog::canceled, this, [&] { cancel = true; });
    QTimer tick;
    connect(&tick, &QTimer::timeout, this, [&] { dlg.setValue(progress); });
    tick.start(50);
    w.setFuture(QtConcurrent::run([&] {
        return media::splitIntoParts(src, dir, base, &err, [&](double f) { progress = int(f * 100); return !cancel.load(); });
    }));
    loop.exec();
    tick.stop();
    dlg.close();
    const QStringList out = w.result();
    if (out.isEmpty()) { if (!cancel) QMessageBox::warning(this, tr("Couldn't split"), err); return; }
    for (const auto &p : out) ClipLibrary::instance().add(p, game, c ? c->kind : "clip");
    Toast::show(tr("Split into %1 parts").arg(out.size()), tr("Each part is 20 minutes or less."));
}

void EditorPage::importVideo() {
    const QString f = QFileDialog::getOpenFileName(this, tr("Import a video"), {}, tr("Videos (*.mp4 *.mkv *.mov)"));
    if (f.isEmpty()) return;
    const media::Info info = media::probe(f);
    if (!info.ok) { QMessageBox::warning(this, tr("Import"), info.error); return; }
    const QString dir = ClipLibrary::instance().folderFor("Imported", "clip");
    const QString base = safeFileName(QFileInfo(f).completeBaseName());
    if (info.duration > media::kMaxClipSeconds) {
        if (QMessageBox::question(this, tr("Long video"), tr("This video is %1. Clips can be at most 20 minutes, so it will be imported as %2 parts. Continue?")
                                      .arg(media::formatDuration(info.duration)).arg(int(std::ceil(info.duration / media::kSplitSeconds)))) != QMessageBox::Yes)
            return;
        QString err;
        QProgressDialog dlg(tr("Importing..."), {}, 0, 0, this);
        dlg.setWindowModality(Qt::WindowModal);
        dlg.show();
        QFutureWatcher<QStringList> w;
        QEventLoop loop;
        connect(&w, &QFutureWatcher<QStringList>::finished, &loop, &QEventLoop::quit);
        w.setFuture(QtConcurrent::run([&] { return media::splitIntoParts(f, dir, base, &err); }));
        loop.exec();
        const QStringList out = w.result();
        if (out.isEmpty()) { QMessageBox::warning(this, tr("Import"), err); return; }
        for (const auto &p : out) ClipLibrary::instance().add(p, "Imported", "clip");
        open(out.first());
        return;
    }
    QString dst = dir + "/" + base + "." + QFileInfo(f).suffix();
    for (int n = 2; QFile::exists(dst); ++n) dst = dir + "/" + base + QStringLiteral(" (%1).").arg(n) + QFileInfo(f).suffix();
    if (!QFile::copy(f, dst)) { QMessageBox::warning(this, tr("Import"), tr("Couldn't copy the video into your library.")); return; }
    ClipLibrary::instance().add(dst, "Imported", "clip");
    open(dst);
}

} // namespace kl
