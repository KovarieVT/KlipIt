#pragma once
#include <QPixmap>
#include <QTimer>
#include <QWidget>

namespace kl {

// The small "Clip saved" card in the corner of the screen. It never takes
// focus or clicks, so it can't pull you out of a game.
class Toast : public QWidget {
    Q_OBJECT
public:
    static void show(const QString &title, const QString &text, const QPixmap &thumb = {}, const QRect &onMonitor = {});

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Toast();
    QString m_title, m_text;
    QPixmap m_thumb;
    QTimer m_hide;
};

} // namespace kl
