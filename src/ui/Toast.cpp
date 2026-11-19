#include "ui/Toast.h"
#include "core/Theme.h"
#include "ui/Art.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>

namespace kl {

Toast::Toast()
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(360, 92);
    m_hide.setSingleShot(true);
    connect(&m_hide, &QTimer::timeout, this, &QWidget::hide);
}

void Toast::show(const QString &title, const QString &text, const QPixmap &thumb, const QRect &onMonitor) {
    static QPointer<Toast> t;
    if (!t) t = new Toast;
    t->m_title = title;
    t->m_text = text;
    t->m_thumb = thumb;
    QRect area = onMonitor;
    if (area.isEmpty() && QApplication::primaryScreen()) area = QApplication::primaryScreen()->availableGeometry();
    else if (QScreen *s = QApplication::screenAt(area.center())) area = s->availableGeometry();
    t->move(area.right() - t->width() - 24, area.top() + 24);
    t->QWidget::show();
    t->raise();
    t->update();
    t->m_hide.start(3800);
}

void Toast::paintEvent(QPaintEvent *) {
    const auto &pal = theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0, pal.wine); g.setColorAt(1, pal.panel);
    p.setPen(QPen(QColor(184, 178, 191, 140), 1));
    p.setBrush(g);
    p.drawRoundedRect(r, 12, 12);
    p.setPen(Qt::NoPen);
    p.setBrush(pal.accent);
    p.drawRoundedRect(QRectF(r.left(), r.top() + 14, 3, r.height() - 28), 1.5, 1.5);
    QRectF icon(r.left() + 14, r.top() + 14, 64, r.height() - 28);
    if (!m_thumb.isNull()) {
        QPainterPath clip; clip.addRoundedRect(icon.adjusted(-8, 0, 30, 0), 6, 6);
        p.save(); p.setClipPath(clip);
        p.drawPixmap(icon.adjusted(-8, 0, 30, 0).toRect(), m_thumb.scaled(icon.adjusted(-8, 0, 30, 0).size().toSize(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        p.restore();
        icon.adjust(0, 0, 30, 0);
    } else {
        art::paintLens(p, QRectF(icon.left(), icon.top(), icon.height(), icon.height()), 0.4, 8);
        icon.setWidth(icon.height());
    }
    const QRectF tr(icon.right() + 14, r.top() + 16, r.right() - icon.right() - 26, 24);
    QFont tf(theme::headingFamily(), 12, QFont::Bold);
    p.setFont(tf);
    p.setPen(pal.text);
    p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(tf).elidedText(m_title, Qt::ElideRight, int(tr.width())));
    QFont bf(theme::uiFamily(), 9);
    p.setFont(bf);
    p.setPen(pal.muted);
    p.drawText(QRectF(tr.left(), tr.bottom() + 2, tr.width(), 36), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_text);
}

} // namespace kl
