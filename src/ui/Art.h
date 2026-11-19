#pragma once
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QRectF>

namespace kl::art {

// The KlipIt mark: a gothic camera lens. An antique-silver lens barrel with a
// cathedral rose-window rim, crimson aperture blades, a crescent-moon glint
// on the glass and a crimson gemstone at the heart.
// `glow` 0..1 adds the crimson halo used on the splash screen.
void paintLens(QPainter &p, const QRectF &r, double glow = 0.0, double apertureTurn = 0.0);
QImage lensImage(int size, double glow = 0.0);

// A thin silver ornamental divider with a small diamond in the middle.
void paintDivider(QPainter &p, const QRectF &r, const QColor &line);
// Victorian corner flourish for frames (corner 0..3 = TL, TR, BR, BL).
void paintCorner(QPainter &p, const QPointF &at, double size, int corner, const QColor &c);
// "KlipIt" wordmark in the heading font with a silver gradient.
void paintWordmark(QPainter &p, const QRectF &r, double pointSize);

} // namespace kl::art
