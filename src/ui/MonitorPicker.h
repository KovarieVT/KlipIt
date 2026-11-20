#pragma once
#include <QWidget>

namespace kl {

// Your monitors drawn to scale, like Windows' display settings. Click one to
// record it, or "Automatic" to follow whichever monitor your game is on.
class MonitorPicker : public QWidget {
    Q_OBJECT
public:
    explicit MonitorPicker(QWidget *parent = nullptr);
    QSize sizeHint() const override { return {560, 190}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    QVector<QRectF> layoutRects() const;
    QRectF autoRect() const;
    void choose(int index);   // -1 = automatic
    int m_hover = -2;
};

} // namespace kl
