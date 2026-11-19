#pragma once
#include <QColor>
#include <QString>
#include <QVector>

class QApplication;

namespace kl::theme {

struct Palette {
    QString id, name;
    QColor bg;        // window background
    QColor panel;     // sidebar / cards
    QColor raised;    // inputs, hovered rows
    QColor border;    // thin borders
    QColor text, muted;
    QColor accent;    // crimson highlight
    QColor accentDeep;// burgundy
    QColor wine;      // darkest accent tint
    QColor silver;    // ornament / outline
    bool   light = false;
};

const QVector<Palette> &palettes();          // black (default), darkgrey, white, blue, purple
const Palette &byId(const QString &id);
const Palette &current();

void loadFonts();                            // Cinzel (headings) + Inter (interface)
void apply(QApplication &app, const QString &themeId);

QString headingFamily();                     // "Cinzel" when bundled
QString uiFamily();                          // "Inter" when bundled

} // namespace kl::theme
