#include "ui/Splash.h"
#include "core/Theme.h"
#include "ui/Art.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QScreen>
#include <QtMath>

namespace kl {

Splash::Splash(bool animated) : QWidget(nullptr, Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), m_animated(animated) {
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(560, 380);
    if (QScreen *s = QApplication::primaryScreen()) move(s->geometry().center() - rect().center());
    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < 70; ++i)
        m_particles << Particle{rng->bounded(560.0), rng->bounded(380.0), 0.8 + rng->bounded(2.4), 8 + rng->bounded(26.0),
                                rng->bounded(16.0) - 8, rng->bounded(6.28), i % 4 == 0};
    m_msg = tr("Awakening your capture engine...");
    m_clock.start();
    connect(&m_tick, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    if (m_animated) m_tick.start(16);
}

void Splash::setProgress(int pct, const QString &message) {
    m_progress = qBound(0, pct, 100) / 100.0;
    if (!message.isEmpty() && message != m_msg) { m_prevMsg = m_msg; m_msg = message; m_msgAt = m_clock.elapsed(); }
    if (!m_animated) m_shown = m_progress;
    repaint();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void Splash::finish(QWidget *w) {
    // let the animation breathe for at least ~1.6 s, then fade out over 0.35 s
    const qint64 wait = m_animated ? qMax<qint64>(0, 1600 - m_clock.elapsed()) : 0;
    m_progress = 1.0;
    QTimer::singleShot(int(wait), this, [this, w] {
        if (w) { w->show(); w->raise(); w->activateWindow(); }
        if (!m_animated) { close(); deleteLater(); return; }
        auto *fade = new QTimer(this);
        connect(fade, &QTimer::timeout, this, [this, fade] {
            m_fade -= 0.06;
            setWindowOpacity(qMax(0.0, m_fade));
            if (m_fade <= 0) { fade->stop(); close(); deleteLater(); }
        });
        fade->start(16);
    });
}

void Splash::paintEvent(QPaintEvent *) {
    const double t = m_clock.elapsed() / 1000.0;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    QPainterPath frame;
    frame.addRoundedRect(r, 18, 18);
    p.setClipPath(frame);

    // obsidian base with slow burgundy light drifting across it
    p.fillRect(r, QColor("#0B090D"));
    for (int i = 0; i < 2; ++i) {
        const double a = t * (0.25 + i * 0.13) + i * 2.1;
        const QPointF c(r.center().x() + std::cos(a) * r.width() * 0.28, r.center().y() + std::sin(a * 1.3) * r.height() * 0.22);
        QRadialGradient g(c, r.width() * (0.55 - i * 0.12));
        g.setColorAt(0, QColor(i ? 0x45 : 0x8F, i ? 0x13 : 0x19, i ? 0x22 : 0x35, i ? 120 : 95));
        g.setColorAt(1, QColor(0, 0, 0, 0));
        p.fillRect(r, g);
    }
    // faint ornamental pattern: a rose-window of thin arcs behind the logo
    p.save();
    p.translate(r.center().x(), 150);
    p.rotate(m_animated ? t * 3.0 : 0);
    p.setPen(QPen(QColor(184, 178, 191, 20), 1));
    for (int i = 0; i < 12; ++i) { p.rotate(30); p.drawEllipse(QPointF(0, -70), 70, 70); }
    p.restore();

    // particles: crimson embers rising, silver sparkles twinkling
    for (auto &pt : m_particles) {
        if (m_animated) {
            pt.y -= pt.speed / 60.0;
            pt.x += std::sin(t + pt.phase) * pt.drift / 120.0;
            if (pt.y < -6) { pt.y = r.height() + 6; pt.x = QRandomGenerator::global()->bounded(r.width()); }
        }
        if (pt.silver) {
            const double tw = 0.5 + 0.5 * std::sin(t * 2.2 + pt.phase * 3);
            const double appear = qBound(0.0, (t - pt.phase * 0.25) / 1.2, 1.0);   // sparkles appear slowly
            QColor c(213, 207, 218, int(200 * tw * appear));
            p.setPen(QPen(c, 1));
            const double s = pt.r * 1.8;
            p.drawLine(QPointF(pt.x - s, pt.y), QPointF(pt.x + s, pt.y));
            p.drawLine(QPointF(pt.x, pt.y - s), QPointF(pt.x, pt.y + s));
        } else {
            QRadialGradient g(QPointF(pt.x, pt.y), pt.r * 3);
            g.setColorAt(0, QColor(181, 42, 72, 200));
            g.setColorAt(1, QColor(143, 25, 53, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawEllipse(QPointF(pt.x, pt.y), pt.r * 3, pt.r * 3);
        }
    }

    // thin silver border with Victorian corners
    p.setClipping(false);
    p.setPen(QPen(QColor(184, 178, 191, 120), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, 18, 18);
    const QColor orn(184, 178, 191, 170);
    art::paintCorner(p, QPointF(14, 14), 30, 0, orn);
    art::paintCorner(p, QPointF(r.right() - 13, 14), 30, 1, orn);
    art::paintCorner(p, QPointF(r.right() - 13, r.bottom() - 13), 30, 2, orn);
    art::paintCorner(p, QPointF(14, r.bottom() - 13), 30, 3, orn);

    // glowing lens: breathes, and its iris turns slowly
    const double pulse = m_animated ? 0.55 + 0.35 * std::sin(t * 2.0) : 0.6;
    const double intro = m_animated ? qBound(0.0, t / 0.7, 1.0) : 1.0;
    const double ease = 1 - std::pow(1 - intro, 3);
    const double size = 150 * (0.85 + 0.15 * ease);
    p.setOpacity(ease);
    art::paintLens(p, QRectF(r.center().x() - size / 2, 150 - size / 2, size, size), pulse, m_animated ? 8 + t * 12 : 8);
    p.setOpacity(1);
    art::paintWordmark(p, QRectF(0, 236, r.width(), 44), 26);
    art::paintDivider(p, QRectF(r.center().x() - 120, 270, 240, 10), QColor(184, 178, 191, 170));

    // loading message, cross-fading between messages
    QFont mf(theme::uiFamily(), 10);
    mf.setItalic(true);
    p.setFont(mf);
    const double k = m_animated ? qBound(0.0, (m_clock.elapsed() - m_msgAt) / 350.0, 1.0) : 1.0;
    if (k < 1 && !m_prevMsg.isEmpty()) {
        p.setPen(QColor(213, 207, 218, int(220 * (1 - k))));
        p.drawText(QRectF(0, 290 - 6 * k, r.width(), 22), Qt::AlignCenter, m_prevMsg);
    }
    p.setPen(QColor(213, 207, 218, int(220 * k)));
    p.drawText(QRectF(0, 296 - 6 * k, r.width(), 22), Qt::AlignCenter, m_msg);

    // elegant progress line with a travelling crimson glint
    if (m_animated) m_shown += (m_progress - m_shown) * 0.12;
    const QRectF bar(r.center().x() - 110, 330, 220, 3);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(184, 178, 191, 45));
    p.drawRoundedRect(bar, 1.5, 1.5);
    QLinearGradient g(bar.topLeft(), bar.topRight());
    g.setColorAt(0, QColor("#8F1935")); g.setColorAt(1, QColor("#B52A48"));
    p.setBrush(g);
    p.drawRoundedRect(QRectF(bar.left(), bar.top(), bar.width() * m_shown, bar.height()), 1.5, 1.5);
    if (m_animated) {
        const double gx = bar.left() + bar.width() * m_shown * (0.5 + 0.5 * std::sin(t * 3));
        QRadialGradient glint(QPointF(gx, bar.center().y()), 10);
        glint.setColorAt(0, QColor(255, 180, 196, 180)); glint.setColorAt(1, QColor(255, 180, 196, 0));
        p.setBrush(glint);
        p.drawEllipse(QPointF(gx, bar.center().y()), 10, 10);
    }
}

} // namespace kl
