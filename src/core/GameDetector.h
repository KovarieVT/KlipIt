#pragma once
#include <QObject>
#include <QRect>
#include <QString>
#include <QTimer>

namespace kl {

struct GameInfo {
    quint32 pid = 0;
    QString exe;          // e.g. VALORANT-Win64-Shipping.exe
    QString path;
    QString name;         // friendly name ("VALORANT")
    quintptr hwnd = 0;
    QString title, windowClass;
    QRect   windowRect;
    bool valid() const { return pid != 0; }
    // libobs' window identifier: "title:class:exe" with ':' and '#' escaped
    QString obsWindowString() const;
};

// Watches the foreground window and decides when a game is running, like
// Medal: fullscreen/borderless programs that use a 3D graphics API, anything
// installed in a game library folder, and games the user added by hand.
// The game stays "current" while you alt-tab, until its process exits.
class GameDetector : public QObject {
    Q_OBJECT
public:
    static GameDetector &instance();
    void start();
    void stop();
    GameInfo current() const { return m_current; }
    // Tests and the "record this program" menu can force a game.
    void setManualGame(const GameInfo &g);
    void clearManualGame();

    static bool isExcludedByDefault(const QString &exeLower);
    static QString friendlyName(const QString &exe, const QString &productName);
    static QString escapeObs(const QString &s);

Q_SIGNALS:
    void gameStarted(const kl::GameInfo &g);
    void gameEnded(const kl::GameInfo &g);

private:
    GameDetector() = default;
    void poll();
    GameInfo m_current;
    bool m_manual = false;
    QTimer m_timer;
#ifdef _WIN32
    void *m_proc = nullptr;   // handle of the current game's process
#endif
};

} // namespace kl
