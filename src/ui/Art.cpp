#include "ui/Art.h"
#include "core/Theme.h"
#include <QPainterPath>
#include <QRadialGradient>
#include <QLinearGradient>
#include <QtMath>

namespace kl::art {

static const QColor kSilver("#B8B2BF"), kMoon("#D5CFDA"), kBone("#E9E1E5"), kCrimson("#B52A48"), kBlood("#8F1935"),
    kBurgundy("#451322"), kWine("#32101F"), kObsidian("#151119"), kMidnight("#0B090D"), kPlum("#38223F");

static QPointF polar(const QPointF &c, double r, double deg) {
    const double a = qDegreesToRadians(deg);
    return {c.x() + r * std::cos(a), c.y() + r * std::sin(a)};
}

void paintLens(QPainter &p, const QRectF &rect, double glow, double turn) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const double s = qMin(rect.width(), rect.height());
    const QPointF c = rect.center();
    const double R = s * 0.5;

    if (glow > 0) {   // crimson halo
        QRadialGradient g(c, R);
        QColor a = kCrimson; a.setAlphaF(0.55 * glow);
        QColor b = kBlood; b.setAlphaF(0.0);
        g.setColorAt(0.55, a); g.setColorAt(1.0, b);
        p.setPen(Qt::NoPen); p.setBrush(g);
        p.drawEllipse(c, R, R);
    }

    // rose-window rim: eight gothic points (pointed arches) around the barrel
    const double rimR = R * 0.80;
    QPainterPath points;
    for (int i = 0; i < 8; ++i) {
        const double a = i * 45.0 - 90.0;
        QPainterPath arch;
        arch.moveTo(polar(c, rimR * 0.86, a - 13));
        arch.quadTo(polar(c, rimR * 1.05, a - 6), polar(c, rimR * 1.15, a));
        arch.quadTo(polar(c, rimR * 1.05, a + 6), polar(c, rimR * 0.86, a + 13));
        arch.closeSubpath();
        points.addPath(arch);
    }
    QLinearGradient silver(rect.topLeft(), rect.bottomRight());
    silver.setColorAt(0.0, kBone); silver.setColorAt(0.45, kSilver); silver.setColorAt(1.0, QColor("#6E6876"));
    p.setPen(QPen(QColor("#4A4452"), s * 0.006));
    p.setBrush(silver);
    p.drawPath(points);

    // outer barrel ring
    p.setBrush(silver);
    p.setPen(QPen(QColor("#3E3946"), s * 0.008));
    p.drawEllipse(c, rimR, rimR);
    // engraved focus ring ticks
    p.setPen(QPen(QColor("#58515F"), s * 0.006, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 48; ++i) {
        const double a = i * 7.5;
        const double inner = i % 4 == 0 ? rimR * 0.86 : rimR * 0.90;
        p.drawLine(polar(c, inner, a), polar(c, rimR * 0.96, a));
    }
    // inner bezel (dark)
    const double bezel = rimR * 0.80;
    QRadialGradient bz(c, bezel);
    bz.setColorAt(0.0, kPlum); bz.setColorAt(1.0, kMidnight);
    p.setPen(QPen(kSilver, s * 0.010));
    p.setBrush(bz);
    p.drawEllipse(c, bezel, bezel);

    // glass
    const double glass = bezel * 0.86;
    QRadialGradient gl(polar(c, glass * 0.25, -120), glass * 1.3);
    gl.setColorAt(0.0, QColor("#2A1A2E")); gl.setColorAt(0.6, kObsidian); gl.setColorAt(1.0, kMidnight);
    p.setPen(Qt::NoPen);
    p.setBrush(gl);
    p.drawEllipse(c, glass, glass);

    // crimson aperture iris: six curved blades meeting in a hexagonal opening
    const double outer = glass * 0.94, open = glass * 0.34;
    auto vertex = [&](int i) { return polar(c, open, turn + i * 60.0); };
    auto rimPt = [&](int i) { return polar(c, outer, turn + i * 60.0 + 58.0); };
    auto ctrl = [&](int i) { return polar(c, (open + outer) * 0.52, turn + i * 60.0 + 14.0); };
    QPainterPath disc;
    disc.addEllipse(c, outer, outer);
    for (int i = 0; i < 6; ++i) {
        QPainterPath blade;
        blade.moveTo(vertex(i));
        blade.quadTo(ctrl(i), rimPt(i));
        blade.arcTo(QRectF(c.x() - outer, c.y() - outer, outer * 2, outer * 2), -(turn + i * 60.0 + 58.0), -60.0);
        blade.quadTo(ctrl(i + 1), vertex(i + 1));
        blade.closeSubpath();
        QRadialGradient bg(c, outer);
        const bool lit = i % 2 == 0;
        bg.setColorAt(0.30, lit ? kCrimson : kBlood);
        bg.setColorAt(1.00, lit ? kBlood : kBurgundy);
        p.setBrush(bg);
        p.setPen(Qt::NoPen);
        p.drawPath(blade.intersected(disc));
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(12, 4, 8, 220), s * 0.006, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 6; ++i) {
        QPainterPath seam;
        seam.moveTo(vertex(i));
        seam.quadTo(ctrl(i), rimPt(i));
        p.drawPath(seam);
    }
    // hexagonal opening, with the gemstone at the heart
    QPolygonF hexagon;
    for (int i = 0; i < 6; ++i) hexagon << vertex(i);
    p.setBrush(kMidnight);
    p.setPen(QPen(kWine, s * 0.006));
    p.drawPolygon(hexagon);
    const double gem = open * 0.58;
    QPolygonF gemShape;
    for (int i = 0; i < 8; ++i) gemShape << polar(c, gem * (i % 2 ? 0.74 : 1.0), i * 45.0 - 90);
    QRadialGradient gg(polar(c, gem * 0.3, -130), gem * 1.6);
    gg.setColorAt(0.0, QColor("#FF8FA6")); gg.setColorAt(0.35, kCrimson); gg.setColorAt(1.0, kBlood);
    p.setBrush(gg);
    p.setPen(QPen(QColor("#2A0A14"), s * 0.004));
    p.drawPolygon(gemShape);
    p.setPen(QPen(QColor(255, 220, 228, 150), s * 0.004));
    for (int i = 0; i < 8; i += 2) p.drawLine(c, polar(c, gem, i * 45.0 - 90));   // facets

    // crescent moon glint on the glass
    QPainterPath moon, bite;
    const QPointF mc = polar(c, glass * 0.55, -135);
    const double mr = glass * 0.20;
    moon.addEllipse(mc, mr, mr);
    bite.addEllipse(mc + QPointF(mr * 0.45, mr * 0.18), mr * 0.88, mr * 0.88);
    QColor moonCol = kMoon; moonCol.setAlphaF(0.85);
    p.setPen(Qt::NoPen);
    p.setBrush(moonCol);
    p.drawPath(moon.subtracted(bite));
    // a tiny star beside the moon
    QColor star = kBone; star.setAlphaF(0.8);
    p.setBrush(star);
    const QPointF sc = polar(c, glass * 0.40, -95);
    const double sr = glass * 0.05;
    QPainterPath sp;
    sp.moveTo(sc + QPointF(0, -sr * 2)); sp.quadTo(sc, sc + QPointF(sr * 2, 0)); sp.quadTo(sc, sc + QPointF(0, sr * 2));
    sp.quadTo(sc, sc + QPointF(-sr * 2, 0)); sp.quadTo(sc, sc + QPointF(0, -sr * 2));
    p.drawPath(sp);
    // glass rim highlight
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(213, 207, 218, 70), s * 0.008));
    p.drawArc(QRectF(c.x() - glass * 0.96, c.y() - glass * 0.96, glass * 1.92, glass * 1.92), 100 * 16, 70 * 16);
    p.restore();
}

QImage lensImage(int size, double glow) {
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    paintLens(p, QRectF(0, 0, size, size), glow, 8);
    return img;
}

void paintDivider(QPainter &p, const QRectF &r, const QColor &line) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const double y = r.center().y(), cx = r.center().x(), d = qMax(3.0, r.height() * 0.35);
    QLinearGradient g(r.left(), y, r.right(), y);
    QColor t = line; t.setAlpha(0);
    g.setColorAt(0, t); g.setColorAt(0.5, line); g.setColorAt(1, t);
    p.setPen(QPen(QBrush(g), 1));
    p.drawLine(QPointF(r.left(), y), QPointF(cx - d * 2, y));
    p.drawLine(QPointF(cx + d * 2, y), QPointF(r.right(), y));
    p.setBrush(line);
    p.setPen(Qt::NoPen);
    p.drawPolygon(QPolygonF{QPointF(cx, y - d), QPointF(cx + d, y), QPointF(cx, y + d), QPointF(cx - d, y)});
    p.restore();
}

void paintCorner(QPainter &p, const QPointF &at, double size, int corner, const QColor &c) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(at);
    p.rotate(corner * 90.0);
    QPainterPath path;
    path.moveTo(0, size);
    path.lineTo(0, size * 0.25);
    path.quadTo(0, 0, size * 0.25, 0);
    path.lineTo(size, 0);
    path.moveTo(size * 0.18, size * 0.62);
    path.cubicTo(size * 0.18, size * 0.3, size * 0.3, size * 0.18, size * 0.62, size * 0.18);
    p.setPen(QPen(c, qMax(1.0, size * 0.035)));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.setBrush(c);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(size * 0.16, size * 0.16), size * 0.045, size * 0.045);
    p.restore();
}

void paintWordmark(QPainter &p, const QRectF &r, double pt) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QFont f(theme::headingFamily());
    f.setPointSizeF(pt);
    f.setWeight(QFont::Bold);
    f.setLetterSpacing(QFont::PercentageSpacing, 108);
    QPainterPath text;
    const QFontMetricsF fm(f);
    const QString word = QStringLiteral("KlipIt");
    const double w = fm.horizontalAdvance(word);
    text.addText(r.center().x() - w / 2, r.center().y() + fm.ascent() / 2 - fm.descent() / 2, f, word);
    QLinearGradient g(0, r.top(), 0, r.bottom());
    g.setColorAt(0.0, kBone); g.setColorAt(0.55, kSilver); g.setColorAt(1.0, QColor("#8A8394"));
    p.setPen(QPen(QColor(11, 9, 13, 160), pt * 0.06));
    p.setBrush(g);
    p.drawPath(text);
    p.restore();
}

} // namespace kl::art
