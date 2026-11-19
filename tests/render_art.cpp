// Renders KlipIt's icons and installer artwork from the same code the app draws with.
// Usage: klipit_render_art <out-dir>
#include <QApplication>
#include <QDir>
#include <QPainter>
#include "core/Theme.h"
#include "ui/Art.h"

using namespace kl;

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    Q_INIT_RESOURCE(resources);
    theme::loadFonts();
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    QDir().mkpath(out);
    for (int s : {16, 24, 32, 48, 64, 128, 256, 512}) art::lensImage(s, 0).save(out + QStringLiteral("/lens_%1.png").arg(s));
    // installer side image (164x314 @100%) and small image (55x55 @100%) at several scales
    for (double sc : {1.0, 1.25, 1.5, 2.0}) {
        const int w = int(164 * sc), h = int(314 * sc);
        QImage side(w, h, QImage::Format_RGB32);
        QPainter p(&side);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, 0, h);
        g.setColorAt(0, QColor("#0B090D")); g.setColorAt(0.55, QColor("#32101F")); g.setColorAt(1, QColor("#0B090D"));
        p.fillRect(side.rect(), g);
        QRadialGradient rg(QPointF(w / 2.0, h * 0.36), w * 0.8);
        rg.setColorAt(0, QColor(143, 25, 53, 120)); rg.setColorAt(1, QColor(0, 0, 0, 0));
        p.fillRect(side.rect(), rg);
        for (int c = 0; c < 4; ++c) {
            const QPointF at[4] = {QPointF(8 * sc, 8 * sc), QPointF(w - 8 * sc, 8 * sc), QPointF(w - 8 * sc, h - 8 * sc), QPointF(8 * sc, h - 8 * sc)};
            art::paintCorner(p, at[c], 26 * sc, c, QColor(184, 178, 191, 190));
        }
        const double ls = w * 0.74;
        art::paintLens(p, QRectF((w - ls) / 2, h * 0.36 - ls / 2, ls, ls), 0.8, 8);
        art::paintWordmark(p, QRectF(0, h * 0.64, w, 40 * sc), 20 * sc);
        art::paintDivider(p, QRectF(w * 0.15, h * 0.75, w * 0.7, 10 * sc), QColor(184, 178, 191, 200));
        p.end();
        side.save(out + QStringLiteral("/wizard_side_%1.bmp").arg(int(sc * 100)));
        const int ss = int(55 * sc);
        QImage small(ss, ss, QImage::Format_RGB32);
        small.fill(QColor("#151119"));
        QPainter q(&small);
        art::paintLens(q, QRectF(ss * 0.06, ss * 0.06, ss * 0.88, ss * 0.88), 0.0, 8);
        q.end();
        small.save(out + QStringLiteral("/wizard_small_%1.bmp").arg(int(sc * 100)));
    }
    return 0;
}
