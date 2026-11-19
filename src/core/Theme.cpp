#include "core/Theme.h"
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QPalette>

namespace kl::theme {

static QString g_current = "black";
static QString g_heading = "Cinzel", g_ui = "Inter";

const QVector<Palette> &palettes() {
    static const QVector<Palette> p = {
        // id, name, bg, panel, raised, border, text, muted, accent, accentDeep, wine, silver, light
        {"black", "Black", QColor("#0B090D"), QColor("#151119"), QColor("#201B25"), QColor("#352D3B"),
         QColor("#E9E1E5"), QColor("#9E96A6"), QColor("#B52A48"), QColor("#8F1935"), QColor("#32101F"), QColor("#B8B2BF"), false},
        {"darkgrey", "Dark grey", QColor("#18171A"), QColor("#201F23"), QColor("#2B2A2F"), QColor("#3D3B42"),
         QColor("#ECE8EE"), QColor("#A39FA9"), QColor("#B52A48"), QColor("#8F1935"), QColor("#3A1A26"), QColor("#C2BDC8"), false},
        {"white", "White", QColor("#ECE7EA"), QColor("#F7F3F5"), QColor("#FFFFFF"), QColor("#D5CFDA"),
         QColor("#1F1823"), QColor("#6D6475"), QColor("#8F1935"), QColor("#6B1128"), QColor("#F1DDE3"), QColor("#8C8594"), true},
        {"blue", "Blue", QColor("#080C15"), QColor("#0E1522"), QColor("#152034"), QColor("#26365A"),
         QColor("#E3E9F6"), QColor("#8D9DBC"), QColor("#4A78E0"), QColor("#2A4A9E"), QColor("#0F1C3A"), QColor("#AEB9D0"), false},
        {"purple", "Purple", QColor("#0E0913"), QColor("#181021"), QColor("#231830"), QColor("#3B2A4F"),
         QColor("#EDE4F5"), QColor("#A493B8"), QColor("#8A4FD3"), QColor("#5E2E9C"), QColor("#2A1740"), QColor("#BDB2CC"), false},
    };
    return p;
}

const Palette &byId(const QString &id) {
    for (const auto &p : palettes()) if (p.id == id) return p;
    return palettes().first();
}
const Palette &current() { return byId(g_current); }
QString headingFamily() { return g_heading; }
QString uiFamily() { return g_ui; }

void loadFonts() {
    for (const char *f : {":/fonts/Cinzel-Regular.ttf", ":/fonts/Cinzel-Bold.ttf", ":/fonts/Inter-Regular.ttf", ":/fonts/Inter-Bold.ttf",
                          ":/fonts/CormorantGaramond-SemiBold.ttf"})
        QFontDatabase::addApplicationFont(QString::fromLatin1(f));
    if (!QFontDatabase::families().contains("Cinzel")) g_heading = "Georgia";
    if (!QFontDatabase::families().contains("Inter")) g_ui = "Segoe UI";
}

static QString rgba(const QColor &c, int a) { return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(a); }

void apply(QApplication &app, const QString &themeId) {
    g_current = byId(themeId).id;
    const Palette &p = current();
    QFile f(":/theme/klipit.qss");
    QString qss = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    const QList<QPair<QString, QString>> tok = {
        {"@accentdeep", p.accentDeep.name()}, {"@accenthover", p.accent.lighter(118).name()},
        {"@accentsoft", rgba(p.accent, 46)}, {"@accentglow", rgba(p.accent, 90)},
        {"@accent", p.accent.name()}, {"@raised", p.raised.name()}, {"@panel", p.panel.name()},
        {"@border", p.border.name()}, {"@silverline", rgba(p.silver, p.light ? 120 : 70)},
        {"@silver", p.silver.name()}, {"@muted", p.muted.name()}, {"@text", p.text.name()},
        {"@wine", p.wine.name()}, {"@bg", p.bg.name()}, {"@headingfont", g_heading}, {"@uifont", g_ui},
        {"@ontext", p.light ? QStringLiteral("#FFFFFF") : QStringLiteral("#FFF5F7")},
    };
    for (const auto &[k, v] : tok) qss.replace(k, v);
    QPalette pal;
    pal.setColor(QPalette::Window, p.bg);
    pal.setColor(QPalette::WindowText, p.text);
    pal.setColor(QPalette::Base, p.raised);
    pal.setColor(QPalette::AlternateBase, p.panel);
    pal.setColor(QPalette::Text, p.text);
    pal.setColor(QPalette::Button, p.raised);
    pal.setColor(QPalette::ButtonText, p.text);
    pal.setColor(QPalette::Highlight, p.accent);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, p.panel);
    pal.setColor(QPalette::ToolTipText, p.text);
    pal.setColor(QPalette::PlaceholderText, p.muted);
    app.setPalette(pal);
    QFont font(g_ui, 10);
    app.setFont(font);
    app.setStyleSheet(qss);
}

} // namespace kl::theme
