#include "ui/MonitorPicker.h"
#include "core/Settings.h"
#include "core/Theme.h"
#include "engine/Recorder.h"
#include <QMouseEvent>
#include <QPainter>

namespace kl {

MonitorPicker::MonitorPicker(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(190);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    connect(&Settings::instance(), &Settings::changed, this, [this](const QString &k) { if (k == "capture.display") update(); });
}

QRectF MonitorPicker::autoRect() const { return QRectF(0, 0, 170, 40); }

QVector<QRectF> MonitorPicker::layoutRects() const {
    const auto mons = Recorder::monitors();
    QRect all;
    for (const auto &m : mons) all = all.united(m.rect);
    QVector<QRectF> out;
    if (all.isEmpty()) return out;
    const QRectF area(190, 8, width() - 198, height() - 16);
    const double scale = qMin(area.width() / all.width(), area.height() / all.height());
    const double ox = area.left() + (area.width() - all.width() * scale) / 2, oy = area.top() + (area.height() - all.height() * scale) / 2;
    for (const auto &m : mons)
        out << QRectF(ox + (m.rect.x() - all.x()) * scale, oy + (m.rect.y() - all.y()) * scale, m.rect.width() * scale, m.rect.height() * scale)
                   .adjusted(3, 3, -3, -3);
    return out;
}

void MonitorPicker::paintEvent(QPaintEvent *) {
    const auto &pal = theme::current();
    const int sel = Settings::instance().i("capture.display");
    const auto mons = Recorder::monitors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // "Automatic" choice
    const QRectF a = autoRect();
    const bool autoSel = sel < 0;
    p.setPen(QPen(autoSel ? pal.accent : m_hover == -1 ? pal.silver : pal.border, autoSel ? 2 : 1));
    p.setBrush(autoSel ? QColor(pal.accent.red(), pal.accent.green(), pal.accent.blue(), 46) : pal.raised);
    p.drawRoundedRect(a, 10, 10);
    p.setPen(pal.text);
    QFont f(theme::uiFamily(), 10, autoSel ? QFont::Bold : QFont::Normal);
    p.setFont(f);
    p.drawText(a, Qt::AlignCenter, tr("Automatic"));
    p.setPen(pal.muted);
    QFont small(theme::uiFamily(), 8);
    p.setFont(small);
    p.drawText(QRectF(0, a.bottom() + 6, 175, 60), Qt::AlignLeft | Qt::TextWordWrap,
               tr("Follows the monitor your game is on. Your main monitor when no game is open."));

    const auto rects = layoutRects();
    for (int i = 0; i < rects.size(); ++i) {
        const QRectF r = rects[i];
        const bool on = sel == i, hov = m_hover == i;
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0, on ? pal.accentDeep : pal.raised);
        g.setColorAt(1, on ? pal.wine : pal.panel);
        p.setPen(QPen(on ? pal.accent : hov ? pal.silver : pal.border, on ? 2.2 : 1.2));
        p.setBrush(g);
        p.drawRoundedRect(r, 6, 6);
        QFont big(theme::headingFamily(), qBound(12, int(r.height() / 3.2), 30), QFont::Bold);
        p.setFont(big);
        p.setPen(on ? QColor(Qt::white) : pal.text);
        p.drawText(r.adjusted(0, -10, 0, -10), Qt::AlignCenter, QString::number(i + 1));
        p.setFont(small);
        p.setPen(on ? QColor(255, 255, 255, 210) : pal.muted);
        p.drawText(r.adjusted(4, 0, -4, -6), Qt::AlignHCenter | Qt::AlignBottom,
                   QStringLiteral("%1×%2%3").arg(mons[i].rect.width()).arg(mons[i].rect.height()).arg(mons[i].primary ? tr(" · main") : QString()));
    }
}

void MonitorPicker::choose(int index) {
    if (Settings::instance().i("capture.display") == index) return;
    Settings::instance().set("capture.display", index);
    Settings::instance().save();
    Recorder::instance().applySettings();
    update();
}

void MonitorPicker::mousePressEvent(QMouseEvent *e) {
    if (autoRect().contains(e->position())) { choose(-1); return; }
    const auto rects = layoutRects();
    for (int i = 0; i < rects.size(); ++i)
        if (rects[i].contains(e->position())) { choose(i); return; }
}

void MonitorPicker::mouseMoveEvent(QMouseEvent *e) {
    int h = -2;
    if (autoRect().contains(e->position())) h = -1;
    const auto rects = layoutRects();
    for (int i = 0; i < rects.size(); ++i) if (rects[i].contains(e->position())) h = i;
    if (h != m_hover) { m_hover = h; update(); }
}

void MonitorPicker::leaveEvent(QEvent *) { m_hover = -2; update(); }

} // namespace kl
