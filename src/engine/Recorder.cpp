#include "engine/Recorder.h"
#include "core/ClipLibrary.h"
#include "core/MediaTools.h"
#include "core/Paths.h"
#include "core/Settings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QRegularExpression>
#include <QScreen>
#include <QThread>
#include <QtConcurrent>
#include <cmath>
#include <graphics/vec4.h>

#ifdef __linux__
#include <obs-nix-platform.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

namespace kl {

static constexpr int kKeyintSec = 1;   // one keyframe per second: trims and splits land within a second

Recorder &Recorder::instance() {
    static Recorder r;
    return r;
}

// ------------------------------------------------------------------ helpers
QString Recorder::profileKey(const QString &exe) {
    QString k = exe.toLower();
    k.replace('.', '_');
    return "profiles." + k;
}

QVariant Recorder::effective(const QString &key) const {
    auto &s = Settings::instance();
    if (m_game.valid()) {
        const QVariant v = s.get(profileKey(m_game.exe) + "." + key);
        if (v.isValid() && !v.isNull() && v.toString() != "default") return v;
    }
    if (key == "audioMode") return s.get("audio.mode");
    return s.get("capture." + key);
}

int Recorder::preSeconds() const { return qBound(1, effective("clipSeconds").toInt(), int(media::kMaxClipSeconds)); }
int Recorder::postSeconds() const {
    const int post = qBound(0, Settings::instance().i("capture.postSeconds"), int(media::kMaxClipSeconds) - 1);
    return qMin(post, int(media::kMaxClipSeconds) - preSeconds());
}

int Recorder::bitrateKbps(int height, int fps, const QString &quality) {
    double base = height <= 720 ? 9000 : height <= 1080 ? 16000 : height <= 1440 ? 26000 : 40000;
    base *= quality == "low" ? 0.5 : quality == "medium" ? 0.72 : quality == "ultra" ? 1.45 : 1.0;
    if (fps <= 30) base *= 0.62;
    else if (fps > 60) base *= 1.55;
    return int(base);
}

QSize Recorder::canvasSize() const {
    obs_video_info ovi{};
    if (!m_started || !obs_get_video_info(&ovi)) return {};
    return {int(ovi.output_width), int(ovi.output_height)};
}

double Recorder::estimatedBufferMB() const {
    const QSize c = canvasSize().isValid() ? canvasSize() : QSize(2560, 1440);
    const int kbps = bitrateKbps(c.height(), effective("fps").toInt(), effective("quality").toString());
    return (kbps + 320.0) * clipSeconds() / 8.0 / 1024.0;
}

QVector<MonitorInfo> Recorder::monitors() {
    QVector<MonitorInfo> out;
#ifdef _WIN32
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM p) -> BOOL {
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(m, &mi)) {
            auto *v = reinterpret_cast<QVector<MonitorInfo> *>(p);
            const RECT r = mi.rcMonitor;
            MonitorInfo info;
            info.rect = QRect(QPoint(r.left, r.top), QPoint(r.right - 1, r.bottom - 1));
            info.primary = mi.dwFlags & MONITORINFOF_PRIMARY;
            info.name = QStringLiteral("%1x%2").arg(info.rect.width()).arg(info.rect.height());
            if (info.primary) v->prepend(info); else v->append(info);
        }
        return TRUE;
    }, LPARAM(&out));
#else
    for (QScreen *s : QGuiApplication::screens()) {
        MonitorInfo info;
        const qreal dpr = s->devicePixelRatio();
        info.rect = QRect(s->geometry().topLeft() * dpr, s->geometry().size() * dpr);
        info.primary = s == QGuiApplication::primaryScreen();
        info.name = QStringLiteral("%1x%2").arg(info.rect.width()).arg(info.rect.height());
        if (info.primary) out.prepend(info); else out.append(info);
    }
#endif
    for (int i = 0; i < out.size(); ++i)
        out[i].name = QObject::tr("Display %1 (%2%3)").arg(i + 1).arg(out[i].name, out[i].primary ? QObject::tr(", main") : QString());
    if (out.isEmpty()) out << MonitorInfo{QObject::tr("Display 1"), QRect(0, 0, 1920, 1080), true};
    return out;
}

QRect Recorder::targetMonitor() const {
    const auto mons = monitors();
    if (m_game.valid() && !m_game.windowRect.isEmpty()) {
        const QPoint c = m_game.windowRect.center();
        for (const auto &m : mons) if (m.rect.contains(c)) return m.rect;
    }
    const int idx = Settings::instance().i("capture.monitor");
    return mons.value(qBound(0, idx, int(mons.size()) - 1)).rect;
}

// ------------------------------------------------------------------ lifecycle
bool Recorder::resetVideoFor(const QRect &monitor, QString *error) {
    auto &s = Settings::instance();
    int w = monitor.width() > 0 ? monitor.width() : 1920, h = monitor.height() > 0 ? monitor.height() : 1080;
    if (qEnvironmentVariableIsSet("KLIPIT_TEST_CANVAS")) {   // tests: fixed small canvas
        const auto p = qEnvironmentVariable("KLIPIT_TEST_CANVAS").split('x');
        w = p.value(0).toInt(); h = p.value(1).toInt();
    }
    const QString res = s.s("capture.resolution");
    const int cap = res == "720" ? 720 : res == "1080" ? 1080 : res == "1440" ? 1440 : 0;
    int ow = w, oh = h;
    if (cap > 0 && h > cap) { oh = cap; ow = int(std::lround(double(w) * cap / h)); }
    ow &= ~1; oh &= ~1;
    const int fps = qBound(24, effective("fps").toInt(), 240);

    obs_video_info ovi{};
#ifdef _WIN32
    ovi.graphics_module = "libobs-d3d11";
#else
    ovi.graphics_module = "libobs-opengl";
#endif
    ovi.fps_num = uint32_t(fps);
    ovi.fps_den = 1;
    ovi.base_width = uint32_t(ow);   // canvas = output: sources are scaled once, on the GPU
    ovi.base_height = uint32_t(oh);
    ovi.output_width = uint32_t(ow);
    ovi.output_height = uint32_t(oh);
    ovi.output_format = VIDEO_FORMAT_NV12;
    ovi.adapter = 0;
    ovi.gpu_conversion = true;
    ovi.colorspace = VIDEO_CS_709;
    ovi.range = VIDEO_RANGE_PARTIAL;
    ovi.scale_type = OBS_SCALE_BICUBIC;
    const int r = obs_reset_video(&ovi);
    if (r != OBS_VIDEO_SUCCESS) {
        if (error) {
            *error = r == OBS_VIDEO_NOT_SUPPORTED ? tr("Your graphics card or driver doesn't support what KlipIt needs (DirectX 11). Updating the graphics driver usually fixes this.")
                   : r == OBS_VIDEO_MODULE_NOT_FOUND ? tr("A part of KlipIt is missing. Please reinstall KlipIt.")
                   : r == OBS_VIDEO_CURRENTLY_ACTIVE ? tr("Can't change the video size while recording.")
                   : tr("The video system couldn't start (code %1).").arg(r);
        }
        return false;
    }
    m_canvasMonitor = monitor;
    for (obs_sceneitem_t *it : {m_gameItem, m_displayItem}) {
        if (!it) continue;
        vec2 b; vec2_set(&b, float(ow), float(oh));
        obs_sceneitem_set_bounds(it, &b);
    }
    return true;
}

bool Recorder::startup(const std::function<void(int, const QString &)> &progress, QString *error) {
    if (m_started) return true;
    auto report = [&](int p, const QString &m) { if (progress) progress(p, m); };
#ifdef __linux__
    if (QGuiApplication::platformName() == "xcb")
        if (auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
            obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL);
            obs_set_nix_platform_display(x11->display());
        }
#endif
#ifdef _WIN32
    // libobs finds its data relative to the working directory, like OBS itself
    QDir::setCurrent(QCoreApplication::applicationDirPath());
#endif
    report(8, tr("Awakening your capture engine..."));
    const QByteArray cfg = QDir::toNativeSeparators(Paths::ensure(Paths::dataRoot() + "/engine")).toUtf8();
    if (!obs_startup("en-US", cfg.constData(), nullptr)) {
        if (error) *error = tr("The capture engine failed to start.");
        return false;
    }
    report(22, tr("Preparing your highlights..."));
    if (!resetVideoFor(targetMonitor(), error)) { obs_shutdown(); return false; }
    obs_audio_info ai{};
    ai.samples_per_sec = 48000;
    ai.speakers = SPEAKERS_STEREO;
    if (!obs_reset_audio(&ai)) {
        if (error) *error = tr("The audio system couldn't start.");
        obs_shutdown();
        return false;
    }

    report(40, tr("Entering the shadows..."));
    // Only the capture/encoding parts of the engine. Nothing for streaming is loaded.
    for (const char *m : {"obs-ffmpeg", "obs-x264", "obs-nvenc", "obs-qsv11", "win-capture", "win-wasapi", "image-source",
                          "linux-capture", "linux-pulseaudio"})
        obs_add_safe_module(m);
    obs_module_failure_info mfi{};
    obs_load_all_modules2(&mfi);
    for (size_t i = 0; i < mfi.count; ++i) qWarning("Engine part failed to load: %s", mfi.failed_modules[i]);
    obs_module_failure_info_free(&mfi);
    obs_log_loaded_modules();
    obs_post_load_modules();

    report(62, tr("Gathering your memories..."));
    createSources();
    configureAudio();
    m_started = true;

    report(80, tr("Your next masterpiece awaits..."));
    auto &det = GameDetector::instance();
    connect(&det, &GameDetector::gameStarted, this, &Recorder::onGameStarted, Qt::UniqueConnection);
    connect(&det, &GameDetector::gameEnded, this, &Recorder::onGameEnded, Qt::UniqueConnection);
    m_hookTimer.setSingleShot(true);
    connect(&m_hookTimer, &QTimer::timeout, this, [this] {
        if (m_game.valid() && !m_hooked) {
            qWarning("Couldn't hook %s, recording its monitor instead", qPrintable(m_game.exe));
            m_fallback = true;
            useDisplay(targetMonitor());
            Q_EMIT notice(tr("Recording %1's screen").arg(m_game.name),
                          tr("This game blocks game capture, so KlipIt records its monitor instead."));
            Q_EMIT stateChanged();
        }
    });
    if (det.current().valid()) onGameStarted(det.current());
    else refresh();
    report(100, tr("Your next masterpiece awaits..."));
    return true;
}

void Recorder::shutdown() {
    if (!m_started) return;
    m_hookTimer.stop();
    if (recordingActive()) {
        // stop cleanly so the last part gets finalised, then wait briefly for it
        obs_output_stop(m_record);
        for (int i = 0; i < 100 && obs_output_active(m_record); ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(30);
        }
    }
    if (m_replay && obs_output_active(m_replay)) obs_output_force_stop(m_replay);
    if (m_micMeter) { obs_volmeter_destroy(m_micMeter); m_micMeter = nullptr; }
    m_replaySigs.clear(); m_recordSigs.clear(); m_gameSigs.clear();
    m_replay = nullptr; m_record = nullptr;
    m_venc = nullptr; m_aenc.clear();
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) obs_set_output_source(uint32_t(ch), nullptr);
    m_gameItem = m_displayItem = nullptr;
    m_gameAudio = nullptr; m_mic = nullptr; m_gameCap = nullptr; m_display = nullptr; m_scene = nullptr;
    m_started = false;
    obs_shutdown();
    qInfo("Engine shut down");
}

// ------------------------------------------------------------------ sources
static obs_sceneitem_t *addFitted(obs_scene_t *scene, obs_source_t *src) {
    if (!src) return nullptr;
    obs_sceneitem_t *it = obs_scene_add(scene, src);
    obs_video_info ovi{};
    obs_get_video_info(&ovi);
    obs_sceneitem_set_bounds_type(it, OBS_BOUNDS_SCALE_INNER);
    obs_sceneitem_set_bounds_alignment(it, OBS_ALIGN_CENTER);
    vec2 b; vec2_set(&b, float(ovi.base_width), float(ovi.base_height));
    obs_sceneitem_set_bounds(it, &b);
    vec2 pos; vec2_set(&pos, 0, 0);
    obs_sceneitem_set_pos(it, &pos);
    return it;
}

void Recorder::createSources() {
    m_scene = obs_scene_create("KlipIt");
    obs_set_output_source(0, obs_scene_get_source(m_scene));
    const QByteArray testSrc = qgetenv("KLIPIT_TEST_SOURCE");   // engine tests use a generated picture
    if (!testSrc.isEmpty()) {
        OBSDataAutoRelease st = obs_data_create();
        obs_data_set_int(st, "width", 1280);
        obs_data_set_int(st, "height", 720);
        obs_data_set_int(st, "color", 0xFF35193A);
        m_display = obs_source_create(testSrc.constData(), "KlipIt Display", st, nullptr);
    } else {
#ifdef _WIN32
        OBSDataAutoRelease gs = obs_data_create();
        obs_data_set_string(gs, "capture_mode", "window");
        obs_data_set_string(gs, "window", "");
        m_gameCap = obs_source_create("game_capture", "KlipIt Game", gs, nullptr);
        m_display = obs_source_create("monitor_capture", "KlipIt Display", nullptr, nullptr);
#else
        m_display = obs_source_create("xshm_input", "KlipIt Display", nullptr, nullptr);
#endif
    }
    m_gameItem = addFitted(m_scene, m_gameCap);
    m_displayItem = addFitted(m_scene, m_display);
    if (m_gameItem) obs_sceneitem_set_visible(m_gameItem, false);
    if (m_displayItem) obs_sceneitem_set_visible(m_displayItem, false);
    if (m_gameCap) {
        signal_handler_t *sh = obs_source_get_signal_handler(m_gameCap);
        auto hook = [](void *data, calldata_t *) {
            auto *self = static_cast<Recorder *>(data);
            QMetaObject::invokeMethod(self, [self] {
                if (!self->m_hooked) qInfo("Game capture hooked %s", qPrintable(self->m_game.exe));
                self->m_hooked = true;
                self->m_hookTimer.stop();
                if (self->m_fallback && self->m_game.valid()) { self->m_fallback = false; self->useGame(); }
                Q_EMIT self->stateChanged();
            }, Qt::QueuedConnection);
        };
        auto unhook = [](void *data, calldata_t *) {
            auto *self = static_cast<Recorder *>(data);
            QMetaObject::invokeMethod(self, [self] { self->m_hooked = false; Q_EMIT self->stateChanged(); }, Qt::QueuedConnection);
        };
        m_gameSigs.emplace_back(sh, "hooked", hook, this);
        m_gameSigs.emplace_back(sh, "unhooked", unhook, this);
    }
}

void Recorder::useGame() {
    if (m_gameItem) obs_sceneitem_set_visible(m_gameItem, true);
    if (m_displayItem) obs_sceneitem_set_visible(m_displayItem, false);
}

void Recorder::useDisplay(const QRect &monitor) {
    if (!m_display) return;
    if (qgetenv("KLIPIT_TEST_SOURCE").isEmpty()) {
        OBSDataAutoRelease st = obs_source_get_settings(m_display);
#ifdef _WIN32
        // match the monitor by the "@ x,y" position OBS puts in each monitor's name
        obs_properties_t *props = obs_source_properties(m_display);
        if (obs_property_t *p = obs_properties_get(props, "monitor_id")) {
            const QString want = QStringLiteral("@ %1,%2").arg(monitor.x()).arg(monitor.y());
            for (size_t i = 0; i < obs_property_list_item_count(p); ++i) {
                const QString name = QString::fromUtf8(obs_property_list_item_name(p, i));
                if (name.contains(want) || i == 0) obs_data_set_string(st, "monitor_id", obs_property_list_item_string(p, i));
                if (name.contains(want)) break;
            }
        }
        obs_properties_destroy(props);
        obs_data_set_int(st, "method", 0);   // automatic (DXGI duplication)
#else
        int idx = 0;
        const auto mons = monitors();
        for (int i = 0; i < mons.size(); ++i) if (mons[i].rect == monitor) idx = i;
        obs_data_set_int(st, "screen", idx);
#endif
        obs_data_set_bool(st, "capture_cursor", Settings::instance().b("capture.cursor"));
        obs_source_update(m_display, st);
    }
    if (m_displayItem) obs_sceneitem_set_visible(m_displayItem, true);
    if (m_gameItem) obs_sceneitem_set_visible(m_gameItem, false);
}

void Recorder::configureAudio() {
    auto &s = Settings::instance();
    const QString mode = effective("audioMode").toString();
    const bool wantGame = mode == "game" || mode == "both";
    const bool wantMic = mode == "voice" || mode == "both";
    const bool hype = s.b("highlights.auto");
    // Track 1 = everything recorded mixed together (plays in any player).
    // With game + voice, track 2 = game only and track 3 = voice only, for editing.
    const uint32_t gameMix = wantGame ? (mode == "both" ? (1u | 2u) : 1u) : 0u;
    const uint32_t micMix = wantMic ? (mode == "both" ? (1u | 4u) : 1u) : 0u;
#ifdef _WIN32
    const char *gameId = "wasapi_process_output_capture", *micId = "wasapi_input_capture";
#else
    const char *gameId = nullptr, *micId = qgetenv("KLIPIT_TEST_SOURCE").isEmpty() ? "pulse_input_capture" : nullptr;
#endif
    if (wantGame && gameId) {
        if (!m_gameAudio) {
            OBSDataAutoRelease st = obs_data_create();
            obs_data_set_int(st, "priority", 2);   // match the game by its program file
            m_gameAudio = obs_source_create(gameId, "KlipIt Game Audio", st, nullptr);
            if (m_gameAudio) obs_set_output_source(1, m_gameAudio);
        }
        if (m_gameAudio) {
            OBSDataAutoRelease st = obs_source_get_settings(m_gameAudio);
            obs_data_set_string(st, "window", m_game.valid() ? m_game.obsWindowString().toUtf8().constData() : "");
            obs_data_set_int(st, "priority", 2);
            obs_source_update(m_gameAudio, st);
            obs_source_set_audio_mixers(m_gameAudio, gameMix);
            obs_source_set_volume(m_gameAudio, float(qBound(0, s.i("audio.gameVolume"), 200)) / 100.f);
        }
    } else if (m_gameAudio) {
        obs_source_set_audio_mixers(m_gameAudio, 0);
    }
    if ((wantMic || hype) && micId) {
        if (!m_mic) {
            OBSDataAutoRelease st = obs_data_create();
            obs_data_set_string(st, "device_id", s.s("audio.micDevice").isEmpty() ? "default" : s.s("audio.micDevice").toUtf8().constData());
            m_mic = obs_source_create(micId, "KlipIt Voice", st, nullptr);
            if (m_mic) obs_set_output_source(2, m_mic);
        }
        if (m_mic) {
            obs_source_set_audio_mixers(m_mic, micMix);   // 0 = only listened to for highlights, never recorded
            obs_source_set_volume(m_mic, float(qBound(0, s.i("audio.micVolume"), 200)) / 100.f);
            if (hype && !m_micMeter) {
                m_micMeter = obs_volmeter_create(OBS_FADER_LOG);
                obs_volmeter_attach_source(m_micMeter, m_mic);
                obs_volmeter_add_callback(m_micMeter, [](void *data, const float[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
                                                         const float[MAX_AUDIO_CHANNELS]) {
                    float p = -INFINITY;
                    for (int i = 0; i < MAX_AUDIO_CHANNELS; ++i) if (std::isfinite(peak[i])) p = std::max(p, peak[i]);
                    static_cast<Recorder *>(data)->onMicPeak(p);
                }, this);
            } else if (!hype && m_micMeter) {
                obs_volmeter_destroy(m_micMeter);
                m_micMeter = nullptr;
            }
        }
    } else if (m_mic) {
        obs_source_set_audio_mixers(m_mic, 0);
    }
}

// Automatic highlights: when you get loud (a clutch moment), save a clip by itself.
void Recorder::onMicPeak(float db) {
    const float threshold = float(Settings::instance().i("highlights.thresholdDb"));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (db < threshold) { m_loudSinceMs = 0; return; }
    qint64 expected = 0;
    m_loudSinceMs.compare_exchange_strong(expected, now);
    if (now - m_loudSinceMs.load() < 350) return;
    m_loudSinceMs = 0;
    QMetaObject::invokeMethod(this, [this] {
        const qint64 t = QDateTime::currentMSecsSinceEpoch();
        if (!m_game.valid() || !bufferActive() || t - m_lastHighlightMs < 45000) return;
        m_lastHighlightMs = t;
        saveClip(true);
    }, Qt::QueuedConnection);
}

// ------------------------------------------------------------------ games
void Recorder::onGameStarted(const GameInfo &g) {
    if (!m_started) return;
    if (!Settings::instance().get(profileKey(g.exe) + ".enabled").isNull() && !Settings::instance().b(profileKey(g.exe) + ".enabled")) {
        qInfo("Recording is off for %s (game profile)", qPrintable(g.exe));
        return;
    }
    stopBuffer();
    m_game = g;
    m_hooked = false;
    m_fallback = false;
    // remember the game for the Games page
    auto &s = Settings::instance();
    s.set(profileKey(g.exe) + ".name", g.name);
    s.set(profileKey(g.exe) + ".exe", g.exe);
    s.save();
    if (m_gameCap) {
        OBSDataAutoRelease st = obs_source_get_settings(m_gameCap);
        obs_data_set_string(st, "capture_mode", "window");
        obs_data_set_string(st, "window", g.obsWindowString().toUtf8().constData());
        obs_data_set_int(st, "priority", 2);
        obs_data_set_bool(st, "capture_cursor", s.b("capture.cursor"));
        obs_data_set_bool(st, "capture_overlays", false);     // never Discord/Steam overlays
        obs_data_set_bool(st, "allow_transparency", false);
        obs_data_set_bool(st, "anti_cheat_hook", true);
        obs_data_set_bool(st, "limit_framerate", false);
        obs_source_update(m_gameCap, st);
        useGame();
        m_hookTimer.start(12000);
    } else {
        useDisplay(targetMonitor());   // test builds / platforms without game capture
    }
    configureAudio();
    refresh();
    if (bufferActive())
        Q_EMIT notice(tr("Recording %1").arg(g.name), tr("Press %1 to save the last %2.")
                      .arg(s.s("hotkeys.saveClip"), media::formatDuration(clipSeconds())));
    if (effective("sessionRecording").toBool() && !recordingActive()) startRecording(true);
    Q_EMIT stateChanged();
}

void Recorder::onGameEnded(const GameInfo &g) {
    if (!m_game.valid() || g.pid != m_game.pid) return;
    qInfo("Game closed: %s", qPrintable(g.exe));
    m_hookTimer.stop();
    if (recordingActive()) stopRecording();
    stopBuffer();
    m_game = {};
    m_hooked = m_fallback = false;
    if (m_gameCap) {
        OBSDataAutoRelease st = obs_source_get_settings(m_gameCap);
        obs_data_set_string(st, "window", "");
        obs_source_update(m_gameCap, st);
    }
    if (m_gameItem) obs_sceneitem_set_visible(m_gameItem, false);
    configureAudio();
    refresh();
    Q_EMIT stateChanged();
}

QString Recorder::captureMethod() const {
    if (Settings::instance().s("capture.mode") == "screen" && !m_game.valid()) return tr("Screen");
    if (m_game.valid()) {
        if (m_fallback) return tr("Monitor capture");
        return m_hooked ? tr("Game capture") : tr("Hooking the game...");
    }
    return Settings::instance().b("capture.onlyWhenGame") ? tr("Waiting for a game") : tr("Screen");
}

// Decides what should be captured right now and keeps the buffer in step.
void Recorder::refresh() {
    if (!m_started) return;
    auto &s = Settings::instance();
    const bool screenMode = s.s("capture.mode") == "screen";
    if (!m_game.valid()) {
        if (screenMode || !s.b("capture.onlyWhenGame")) useDisplay(targetMonitor());
        else {
            if (m_displayItem) obs_sceneitem_set_visible(m_displayItem, false);
            if (m_gameItem) obs_sceneitem_set_visible(m_gameItem, false);
        }
    }
    const bool want = !m_userPaused && (m_game.valid() || screenMode || !s.b("capture.onlyWhenGame"));
    if (want && !bufferActive()) {
        QString err;
        if (!startBuffer(&err)) { m_lastError = err; Q_EMIT problem(err); }
    } else if (!want && bufferActive()) {
        stopBuffer();
    }
    Q_EMIT stateChanged();
}

void Recorder::setBufferPaused(bool paused) {
    m_userPaused = paused;
    refresh();
}

void Recorder::applySettings() {
    if (!m_started) return;
    configureAudio();
    if (recordingActive()) {   // never interrupt a recording; video changes apply when it ends
        Q_EMIT stateChanged();
        return;
    }
    stopBuffer();
    m_venc = nullptr;
    m_aenc.clear();
    m_forceCpu = false;
    refresh();
}

// ------------------------------------------------------------------ encoders
QVector<EncoderChoice> Recorder::encoders() const {
    QVector<EncoderChoice> out;
    const char *id = nullptr;
    for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i) {
        if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO) continue;
        if (obs_get_encoder_caps(id) & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL)) continue;
        EncoderChoice e;
        e.id = QString::fromUtf8(id);
        e.name = QString::fromUtf8(obs_encoder_get_display_name(id));
        e.codec = QString::fromUtf8(obs_get_encoder_codec(id));
        const QString l = e.id.toLower();
        e.family = l.contains("x264") ? "x264" : l.contains("nvenc") ? "nvenc" : l.contains("amf") ? "amf" : l.contains("qsv") ? "qsv" : "other";
        e.hardware = e.family == "nvenc" || e.family == "amf" || e.family == "qsv";
        if (e.codec == "h264" || e.codec == "hevc") out << e;
    }
    return out;
}

QString Recorder::bestEncoder(const QString &codec) const {
    const auto list = encoders();
    // GPU encoders first so games keep their frame rate; texture variants avoid copying frames to the CPU.
    for (const char *fam : {"nvenc", "amf", "qsv", "x264", "other"}) {
        QString pick;
        for (const auto &e : list) {
            if (e.family != fam || e.codec != codec) continue;
            if (pick.isEmpty() || e.id.contains("tex")) pick = e.id;
        }
        if (!pick.isEmpty()) return pick;
    }
    return codec == "h264" ? QString() : bestEncoder("h264");
}

void Recorder::ensureEncoders(QString *error) {
    if (m_venc && !m_aenc.empty()) return;
    auto &s = Settings::instance();
    const QString codec = effective("codec").toString() == "hevc" ? "hevc" : "h264";
    QString id = m_forceCpu ? QStringLiteral("obs_x264") : effective("encoder").toString();
    if (id.isEmpty() || id == "auto" || !obs_get_encoder_codec(id.toUtf8().constData())) id = bestEncoder(codec);
    if (id.isEmpty()) { if (error) *error = tr("No video encoder is available."); return; }
    const QSize c = canvasSize();
    const int kbps = bitrateKbps(c.height(), effective("fps").toInt(), effective("quality").toString());
    OBSDataAutoRelease st = obs_data_create();
    const QString l = id.toLower();
    obs_data_set_int(st, "bitrate", kbps);
    obs_data_set_int(st, "max_bitrate", int(kbps * 1.5));
    obs_data_set_int(st, "keyint_sec", kKeyintSec);
    if (l.contains("x264")) {
        obs_data_set_string(st, "rate_control", "CBR");
        obs_data_set_string(st, "preset", "veryfast");
    } else if (l.contains("nvenc")) {
        obs_data_set_string(st, "rate_control", "VBR");
        obs_data_set_string(st, "preset", "p5");
        obs_data_set_string(st, "preset2", "p5");
        obs_data_set_string(st, "tune", "hq");
        obs_data_set_string(st, "multipass", "qres");
        obs_data_set_string(st, "profile", "high");
    } else if (l.contains("amf")) {
        obs_data_set_string(st, "rate_control", "VBR");
        obs_data_set_string(st, "preset", "balanced");
    } else if (l.contains("qsv")) {
        obs_data_set_string(st, "rate_control", "VBR");
        obs_data_set_string(st, "target_usage", "TU4");
    }
    m_venc = obs_video_encoder_create(id.toUtf8().constData(), "KlipIt Video", st, nullptr);
    if (!m_venc && id != "obs_x264") {
        qWarning("Encoder %s couldn't be created, using the CPU encoder", qPrintable(id));
        id = "obs_x264";
        obs_data_set_string(st, "rate_control", "CBR");
        obs_data_set_string(st, "preset", "veryfast");
        m_venc = obs_video_encoder_create("obs_x264", "KlipIt Video", st, nullptr);
    }
    if (!m_venc) { if (error) *error = tr("Couldn't create the video encoder."); return; }
    obs_encoder_set_video(m_venc, obs_get_video());
    m_encoderId = id;
    m_encoderName = QString::fromUtf8(obs_encoder_get_display_name(id.toUtf8().constData()));
    const int tracks = effective("audioMode").toString() == "both" ? 3 : 1;
    for (int t = 0; t < tracks; ++t) {
        OBSDataAutoRelease as = obs_data_create();
        obs_data_set_int(as, "bitrate", 192);
        OBSEncoderAutoRelease ae = obs_audio_encoder_create("ffmpeg_aac", QStringLiteral("KlipIt Audio %1").arg(t + 1).toUtf8().constData(),
                                                            as, size_t(t), nullptr);
        if (ae) { obs_encoder_set_audio(ae, obs_get_audio()); m_aenc.push_back(std::move(ae)); }
    }
    if (m_aenc.empty()) { m_venc = nullptr; if (error) *error = tr("Couldn't create the audio encoder."); }
}

void Recorder::releaseEncodersIfIdle() {
    if (bufferActive() || recordingActive()) return;
    m_venc = nullptr;
    m_aenc.clear();
}

// ------------------------------------------------------------------ outputs
void Recorder::connectOutput(obs_output_t *out, const QString &tag, std::vector<OBSSignal> &sigs) {
    struct Ctx { Recorder *r; QString tag, what; };
    for (const char *what : {"start", "stop", "saved", "file_changed"}) {
        auto *ctx = new Ctx{this, tag, QString::fromLatin1(what)};   // tiny, lives for the process so late callbacks stay safe
        sigs.emplace_back(obs_output_get_signal_handler(out), what, [](void *data, calldata_t *cd) {
            auto *c = static_cast<Ctx *>(data);
            const int code = int(calldata_int(cd, "code"));
            const char *nf = nullptr;
            calldata_get_string(cd, "next_file", &nf);
            const QString extra = nf ? QString::fromUtf8(nf) : QString();
            Recorder *r = c->r; const QString tag = c->tag, what = c->what;
            QMetaObject::invokeMethod(r, [r, tag, what, code, extra] { r->onOutputSignal(tag, what, code, extra); }, Qt::QueuedConnection);
        }, ctx);
    }
}

bool Recorder::bufferActive() const { return m_replay && obs_output_active(m_replay); }
bool Recorder::recordingActive() const { return m_record && obs_output_active(m_record); }

bool Recorder::startBuffer(QString *error) {
    if (bufferActive()) return true;
    // match the video size to the monitor we're capturing (only possible while nothing records)
    const QRect mon = targetMonitor();
    if (!recordingActive() && mon != m_canvasMonitor) {
        m_venc = nullptr; m_aenc.clear();
        if (!resetVideoFor(mon, error)) return false;
    }
    ensureEncoders(error);
    if (!m_venc) return false;
    const QString incoming = Paths::ensure(ClipLibrary::instance().root() + "/.incoming");
    OBSDataAutoRelease st = obs_data_create();
    obs_data_set_string(st, "directory", incoming.toUtf8().constData());
    obs_data_set_string(st, "format", "Replay %CCYY-%MM-%DD %hh-%mm-%ss");
    obs_data_set_string(st, "extension", "mp4");
    obs_data_set_bool(st, "allow_spaces", true);
    obs_data_set_int(st, "max_time_sec", clipSeconds());
    obs_data_set_int(st, "max_size_mb", qMax(64, int(estimatedBufferMB() * 1.35) + 64));
    m_replaySigs.clear();
    m_replay = obs_output_create("replay_buffer", "KlipIt Instant Replay", st, nullptr);
    if (!m_replay) { if (error) *error = tr("The instant replay part of the engine is missing. Please reinstall KlipIt."); return false; }
    obs_output_set_video_encoder(m_replay, m_venc);
    for (size_t t = 0; t < m_aenc.size(); ++t) obs_output_set_audio_encoder(m_replay, m_aenc[t], t);
    connectOutput(m_replay, "replay", m_replaySigs);
    if (!obs_output_start(m_replay)) {
        const char *le = obs_output_get_last_error(m_replay);
        const QString msg = le ? QString::fromUtf8(le) : tr("Instant replay couldn't start.");
        // a GPU encoder that refuses to start: fall back to the CPU encoder once
        if (m_encoderId != "obs_x264") {
            qWarning("Replay buffer failed with %s (%s), retrying with x264", qPrintable(m_encoderId), qPrintable(msg));
            m_replaySigs.clear(); m_replay = nullptr; m_venc = nullptr; m_aenc.clear();
            m_forceCpu = true;
            const bool ok = startBuffer(error);
            if (!ok) m_forceCpu = false;
            return ok;
        }
        if (error) *error = msg;
        m_replaySigs.clear(); m_replay = nullptr;
        return false;
    }
    m_lastError.clear();
    qInfo("Instant replay on: %ds, %s, canvas %dx%d", clipSeconds(), qPrintable(m_encoderName), canvasSize().width(), canvasSize().height());
    return true;
}

void Recorder::stopBuffer() {
    if (!m_replay) return;
    if (obs_output_active(m_replay)) obs_output_force_stop(m_replay);   // buffered video is discarded; nothing was saved yet
    m_replaySigs.clear();
    m_replay = nullptr;
    releaseEncodersIfIdle();
}

void Recorder::saveClip(bool highlight) {
    if (!bufferActive()) {
        Q_EMIT notice(tr("Instant replay is off"), m_game.valid() || Settings::instance().s("capture.mode") == "screen"
                          ? tr("Turn instant replay on to save clips.") : tr("Start a game, or switch to screen mode in Capture settings."));
        return;
    }
    const PendingSave p{m_game.valid() ? m_game.name : QStringLiteral("Desktop"), highlight};
    const int post = postSeconds();
    auto doSave = [this, p] {
        if (!bufferActive()) return;
        m_pendingSaves.enqueue(p);
        calldata_t cd{};
        proc_handler_call(obs_output_get_proc_handler(m_replay), "save", &cd);
        calldata_free(&cd);
    };
    if (post > 0) {
        Q_EMIT notice(highlight ? tr("Highlight!") : tr("Clipping..."), tr("Keeping %1 more seconds, then saving.").arg(post));
        QTimer::singleShot(post * 1000, this, doSave);
    } else {
        doSave();
    }
}

static QString uniquePath(const QString &dir, const QString &base, const QString &ext) {
    QString p = dir + "/" + base + "." + ext;
    for (int n = 2; QFile::exists(p); ++n) p = dir + "/" + base + QStringLiteral(" (%1).").arg(n) + ext;
    return p;
}

void Recorder::onReplaySaved(const QString &path) {
    const PendingSave p = m_pendingSaves.isEmpty() ? PendingSave{gameName().isEmpty() ? "Desktop" : gameName(), false} : m_pendingSaves.dequeue();
    if (path.isEmpty() || !QFile::exists(path)) { Q_EMIT problem(tr("The clip couldn't be saved.")); return; }
    auto &lib = ClipLibrary::instance();
    const QString name = safeFileName(p.game) + " " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm-ss");
    const QString dst = uniquePath(lib.folderFor(p.game, "clip"), name, QFileInfo(path).suffix());
    const QString finalPath = QFile::rename(path, dst) ? dst : path;
    lib.add(finalPath, p.game, "clip", p.highlight);
    Q_EMIT clipSaved(finalPath, p.game, p.highlight);
}

void Recorder::toggleRecording() {
    if (recordingActive()) stopRecording();
    else startRecording(false);
}

void Recorder::startRecording(bool session) {
    if (recordingActive()) return;
    QString err;
    if (!bufferActive() && !startBuffer(&err) && !m_venc) {   // recording shares the buffer's encoder
        ensureEncoders(&err);
        if (!m_venc) { Q_EMIT problem(err); return; }
    }
    const QString game = m_game.valid() ? m_game.name : QStringLiteral("Desktop");
    const QString work = Paths::ensure(ClipLibrary::instance().root() + "/.recording");
    const QString kind = session ? tr("Session") : tr("Recording");
    const QString fmt = safeFileName(game) + " " + kind + " %CCYY-%MM-%DD %hh-%mm-%ss";
    const QString first = work + "/" + safeFileName(game) + " " + kind + " " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm-ss") + ".mkv";
    OBSDataAutoRelease st = obs_data_create();
    obs_data_set_string(st, "path", first.toUtf8().constData());
    // Split long recordings into consecutive files a little under 20 minutes each (nothing is lost between parts).
    obs_data_set_bool(st, "split_file", true);
    const int split = qEnvironmentVariableIsSet("KLIPIT_TEST_SPLIT_SECONDS") ? qEnvironmentVariableIntValue("KLIPIT_TEST_SPLIT_SECONDS")
                                                                         : int(media::kSplitSeconds);   // tests use short parts
    obs_data_set_int(st, "max_time_sec", split);
    obs_data_set_int(st, "max_size_mb", 0);
    obs_data_set_string(st, "directory", work.toUtf8().constData());
    obs_data_set_string(st, "format", fmt.toUtf8().constData());
    obs_data_set_string(st, "extension", "mkv");   // crash-safe while recording; turned into MP4 per part
    obs_data_set_bool(st, "allow_spaces", true);
    obs_data_set_bool(st, "allow_overwrite", false);
    m_recordSigs.clear();
    m_record = obs_output_create("ffmpeg_muxer", "KlipIt Recording", st, nullptr);
    if (!m_record) { Q_EMIT problem(tr("The recording part of the engine is missing. Please reinstall KlipIt.")); return; }
    obs_output_set_video_encoder(m_record, m_venc);
    for (size_t t = 0; t < m_aenc.size(); ++t) obs_output_set_audio_encoder(m_record, m_aenc[t], t);
    connectOutput(m_record, "record", m_recordSigs);
    if (!obs_output_start(m_record)) {
        const char *le = obs_output_get_last_error(m_record);
        Q_EMIT problem(le ? QString::fromUtf8(le) : tr("Recording couldn't start."));
        m_recordSigs.clear(); m_record = nullptr;
        return;
    }
    m_recIsSession = session;
    m_recGame = game;
    m_recPart = first;
    m_recStartMs = QDateTime::currentMSecsSinceEpoch();
    Q_EMIT notice(session ? tr("Recording the whole session") : tr("Recording started"),
                  tr("Saved in parts of up to 20 minutes. Press %1 to stop.").arg(Settings::instance().s("hotkeys.toggleRecording")));
    Q_EMIT stateChanged();
}

void Recorder::stopRecording() {
    if (recordingActive()) obs_output_stop(m_record);   // "stop" signal finalises the last part
}

void Recorder::finalizePart(const QString &mkv, const QString &game) {
    if (mkv.isEmpty() || !QFile::exists(mkv)) return;
    const QString dir = ClipLibrary::instance().folderFor(game, "recording");
    (void)QtConcurrent::run([this, mkv, game, dir] {
        const QString mp4 = uniquePath(dir, QFileInfo(mkv).completeBaseName(), "mp4");
        QString err;
        QString result;
        if (media::remux(mkv, mp4, &err)) {
            QFile::remove(mkv);
            result = mp4;
        } else {
            // never throw footage away: keep the original file
            qWarning("Couldn't convert %s to MP4 (%s), keeping the MKV", qPrintable(mkv), qPrintable(err));
            result = uniquePath(dir, QFileInfo(mkv).completeBaseName(), "mkv");
            if (!QFile::rename(mkv, result)) result = mkv;
        }
        QMetaObject::invokeMethod(this, [this, result, game] {
            ClipLibrary::instance().add(result, game, "recording");
            Q_EMIT recordingSaved(result, game);
        }, Qt::QueuedConnection);
    });
}

void Recorder::onOutputSignal(const QString &tag, const QString &what, int code, const QString &extra) {
    if (tag == "replay") {
        if (what == "saved" && m_replay) {
            calldata_t cd{};
            proc_handler_call(obs_output_get_proc_handler(m_replay), "get_last_replay", &cd);
            const char *p = calldata_string(&cd, "path");
            const QString path = p ? QString::fromUtf8(p) : QString();
            calldata_free(&cd);
            onReplaySaved(path);
        } else if (what == "stop" && code != OBS_OUTPUT_SUCCESS) {
            const char *le = m_replay ? obs_output_get_last_error(m_replay) : nullptr;
            m_lastError = le ? QString::fromUtf8(le) : tr("Instant replay stopped unexpectedly (code %1).").arg(code);
            Q_EMIT problem(m_lastError);
        }
        Q_EMIT stateChanged();
    } else if (tag == "record") {
        if (what == "file_changed") {
            finalizePart(m_recPart, m_recGame);
            m_recPart = extra;
        } else if (what == "stop") {
            if (code != OBS_OUTPUT_SUCCESS) {
                const char *le = m_record ? obs_output_get_last_error(m_record) : nullptr;
                Q_EMIT problem(le ? QString::fromUtf8(le) : tr("Recording stopped unexpectedly (code %1).").arg(code));
            }
            finalizePart(m_recPart, m_recGame);
            m_recPart.clear();
            m_recordSigs.clear();
            m_record = nullptr;
            m_recIsSession = false;
            releaseEncodersIfIdle();
            if (!m_game.valid()) refresh();
        }
        Q_EMIT stateChanged();
    }
}

// ------------------------------------------------------------------ screenshots
namespace {
// Screenshots are read back from the graphics card in two steps, one frame apart
// (render + copy, then map), exactly like OBS does, so the copy is finished.
struct ShotJob { obs_source_t *src; uint32_t w, h; gs_texrender_t *tr = nullptr; gs_stagesurf_t *stage = nullptr; bool staged = false; QImage img; };
void renderShot(void *param) {
    auto *j = static_cast<ShotJob *>(param);
    j->tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    j->stage = gs_stagesurface_create(j->w, j->h, GS_RGBA);
    if (j->tr && j->stage && gs_texrender_begin(j->tr, j->w, j->h)) {
        vec4 clear; vec4_zero(&clear);
        gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
        gs_ortho(0.0f, float(j->w), 0.0f, float(j->h), -100.0f, 100.0f);
        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
        obs_source_video_render(j->src);
        gs_blend_state_pop();
        gs_texrender_end(j->tr);
        gs_stage_texture(j->stage, gs_texrender_get_texture(j->tr));
        j->staged = true;
    }
}
void readShot(void *param) {
    auto *j = static_cast<ShotJob *>(param);
    uint8_t *data = nullptr;
    uint32_t linesize = 0;
    if (j->staged && gs_stagesurface_map(j->stage, &data, &linesize)) {
        QImage img(int(j->w), int(j->h), QImage::Format_RGBX8888);
        for (uint32_t y = 0; y < j->h; ++y) memcpy(img.scanLine(int(y)), data + size_t(y) * linesize, size_t(j->w) * 4);
        gs_stagesurface_unmap(j->stage);
        j->img = img;
    }
    if (j->stage) gs_stagesurface_destroy(j->stage);
    if (j->tr) gs_texrender_destroy(j->tr);
    j->stage = nullptr;
    j->tr = nullptr;
}
} // namespace

void Recorder::takeScreenshot() {
    if (!m_started) return;
    const QSize c = canvasSize();
    ShotJob job{obs_scene_get_source(m_scene), uint32_t(c.width()), uint32_t(c.height())};
    obs_enter_graphics();
    renderShot(&job);
    obs_leave_graphics();
    QThread::msleep(40);   // let the copy finish on the GPU (about one frame)
    obs_enter_graphics();
    readShot(&job);
    obs_leave_graphics();
    if (job.img.isNull()) { qWarning("Screenshot failed (staged=%d, %ux%u)", int(job.staged), job.w, job.h); Q_EMIT problem(tr("The screenshot couldn't be taken.")); return; }
    const QString game = m_game.valid() ? m_game.name : QStringLiteral("Desktop");
    const QString dir = ClipLibrary::instance().folderFor(game, "screenshot");
    const QString path = uniquePath(dir, safeFileName(game) + " " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm-ss"), "png");
    if (!job.img.convertToFormat(QImage::Format_RGB888).save(path, "PNG")) { Q_EMIT problem(tr("The screenshot couldn't be saved.")); return; }
    ClipLibrary::instance().add(path, game, "screenshot");
    Q_EMIT screenshotSaved(path, game);
}

QStringList Recorder::loadedModules() const {
    QStringList out;
    obs_enum_modules([](void *p, obs_module_t *m) {
        const char *n = obs_get_module_name(m);
        const char *f = obs_get_module_file_name(m);
        static_cast<QStringList *>(p)->append(QString::fromUtf8(n ? n : f ? f : "?"));
    }, &out);
    return out;
}

} // namespace kl
