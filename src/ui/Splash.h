#pragma once
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QWidget>

namespace kl {

// The gothic loading screen: obsidian background, drifting burgundy light,
// crimson particles rising, silver sparkles fading in, ornamental frame,
// a glowing lens logo and loading messages that fade between each other.
class Splash : public QWidget {
    Q_OBJECT
public:
    explicit Splash(bool animated);
    void setProgress(int pct, const QString &message);
    void finish(QWidget *mainWindow);      // fades out (keeps a short minimum so it never just flickers)

protected:
    void paintEvent(QPaintEvent *) override;

private:
    struct Particle { double x, y, r, speed, drift, phase; bool silver; };
    bool m_animated;
    QTimer m_tick;
    QElapsedTimer m_clock;
    QVector<Particle> m_particles;
    QString m_msg, m_prevMsg;
    qint64 m_msgAt = 0;
    double m_progress = 0, m_shown = 0;
    double m_fade = 1.0;
};

} // namespace kl
