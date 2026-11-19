#pragma once
#include "core/GameDetector.h"
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QRect>
#include <QTimer>
#include <QVector>
#include <atomic>
#include <functional>
#include <obs.hpp>

namespace kl {

struct EncoderChoice {
    QString id, name, family, codec;   // family: nvenc | amf | qsv | x264 | other
    bool hardware = false;
};

struct MonitorInfo { QString name; QRect rect; bool primary = false; };

// The capture engine: OBS's open-source recording core, used only for capture,
// GPU encoding and the replay buffer (KlipIt never streams).
//
//  • game mode: when GameDetector finds a game, KlipIt hooks it with game
//    capture (lowest overhead, captures only the game, never overlays like
//    Discord) and keeps an instant-replay buffer in memory.
//  • if a game can't be hooked (some anti-cheats), it falls back to capturing
//    that game's monitor.
//  • screen mode records a whole monitor all the time.
//  • every saved clip, recording part and edit is limited to 20 minutes; full
//    sessions are split into consecutive parts automatically.
class Recorder : public QObject {
    Q_OBJECT
public:
    static Recorder &instance();

    bool startup(const std::function<void(int, const QString &)> &progress, QString *error);
    void shutdown();
    bool isStarted() const { return m_started; }

    bool bufferActive() const;
    bool bufferPausedByUser() const { return m_userPaused; }
    bool recordingActive() const;
    bool sessionRecording() const { return recordingActive() && m_recIsSession; }
    qint64 recordingStartedMs() const { return m_recStartMs; }
    QString gameName() const { return m_game.valid() ? m_game.name : QString(); }
    GameInfo game() const { return m_game; }
    QString captureMethod() const;
    QString encoderName() const { return m_encoderName; }
    QString lastError() const { return m_lastError; }
    QSize canvasSize() const;

    int preSeconds() const;              // effective for the current game
    int postSeconds() const;
    int clipSeconds() const { return preSeconds() + postSeconds(); }
    QVariant effective(const QString &key) const;   // a capture setting after the current game's profile
    static QString profileKey(const QString &exe);

    QVector<EncoderChoice> encoders() const;
    QString bestEncoder(const QString &codec) const;
    static QVector<MonitorInfo> monitors();
    QStringList loadedModules() const;
    static int bitrateKbps(int height, int fps, const QString &quality);
    double estimatedBufferMB() const;

public Q_SLOTS:
    void saveClip(bool highlight = false);
    void toggleRecording();
    void startRecording(bool session = false);
    void stopRecording();
    void takeScreenshot();
    void setBufferPaused(bool paused);
    void applySettings();

Q_SIGNALS:
    void stateChanged();
    void clipSaved(const QString &path, const QString &game, bool highlight);
    void recordingSaved(const QString &path, const QString &game);
    void screenshotSaved(const QString &path, const QString &game);
    void notice(const QString &title, const QString &text);
    void problem(const QString &message);

private:
    Recorder() = default;
    bool resetVideoFor(const QRect &monitor, QString *error);
    void createSources();
    void configureAudio();
    void refresh();
    void onGameStarted(const GameInfo &g);
    void onGameEnded(const GameInfo &g);
    void useDisplay(const QRect &monitor);
    void useGame();
    bool startBuffer(QString *error = nullptr);
    void stopBuffer();
    void ensureEncoders(QString *error);
    void releaseEncodersIfIdle();
    void onOutputSignal(const QString &tag, const QString &what, int code, const QString &extra);
    void finalizePart(const QString &mkvPath, const QString &game);
    void onReplaySaved(const QString &path);
    void onMicPeak(float db);
    QRect targetMonitor() const;
    void connectOutput(obs_output_t *out, const QString &tag, std::vector<OBSSignal> &sigs);

    bool m_started = false;
    bool m_userPaused = false;
    bool m_forceCpu = false;   // a GPU encoder failed to start; use the CPU encoder until settings change
    GameInfo m_game;
    bool m_hooked = false, m_fallback = false;
    QTimer m_hookTimer;
    QString m_encoderName, m_encoderId, m_lastError;
    QRect m_canvasMonitor;

    OBSSceneAutoRelease   m_scene;
    OBSSourceAutoRelease  m_gameCap, m_display, m_gameAudio, m_mic;
    obs_sceneitem_t      *m_gameItem = nullptr, *m_displayItem = nullptr;
    OBSEncoderAutoRelease m_venc;
    std::vector<OBSEncoderAutoRelease> m_aenc;
    OBSOutputAutoRelease  m_replay, m_record;
    std::vector<OBSSignal> m_replaySigs, m_recordSigs, m_gameSigs;
    obs_volmeter_t       *m_micMeter = nullptr;

    struct PendingSave { QString game; bool highlight; };
    QQueue<PendingSave> m_pendingSaves;
    bool m_recIsSession = false;
    qint64 m_recStartMs = 0;
    QString m_recGame, m_recPart;
    std::atomic<qint64> m_loudSinceMs{0};
    qint64 m_lastHighlightMs = 0;
};

} // namespace kl
