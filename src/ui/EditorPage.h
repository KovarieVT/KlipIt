#pragma once
#include <QWidget>

class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class QLabel;
class QPushButton;
class QSlider;
class QStackedWidget;

namespace kl {

// Timeline with a playhead and draggable start/end handles.
class TrimBar : public QWidget {
    Q_OBJECT
public:
    explicit TrimBar(QWidget *parent = nullptr);
    void setDuration(double s);
    void setPosition(double s);
    void setRange(double a, double b);
    double start() const { return m_a; }
    double end() const { return m_b; }
    QSize sizeHint() const override { return {600, 56}; }

Q_SIGNALS:
    void seekRequested(double s);
    void rangeChanged(double a, double b);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    double xToTime(double x) const;
    double timeToX(double t) const;
    double m_dur = 0, m_pos = 0, m_a = 0, m_b = 0;
    int m_drag = 0;   // 1 start, 2 end, 3 playhead
};

// The Video Editor: watch a clip, set a start and end, and save that part as
// its own new clip (the original stays). Long videos can be split into parts
// of 20 minutes or less. Screenshots open here too.
class EditorPage : public QWidget {
    Q_OBJECT
public:
    explicit EditorPage(QWidget *parent = nullptr);
    void open(const QString &path);
    QString currentPath() const { return m_path; }

Q_SIGNALS:
    void openClip(const QString &path);

private:
    void updateRangeLabel();
    void saveTrim();
    void splitParts();
    void importVideo();
    void refreshInfo();
    QString m_path;
    double m_duration = 0;
    QMediaPlayer *m_player;
    QAudioOutput *m_audio;
    QVideoWidget *m_video;
    QLabel *m_image, *m_title, *m_info, *m_range, *m_time, *m_empty;
    QStackedWidget *m_view;
    TrimBar *m_bar;
    QPushButton *m_play, *m_save, *m_split, *m_fav;
    QWidget *m_tools;
};

} // namespace kl
