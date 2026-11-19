#include "ui/ClipGrid.h"
#include "core/MediaTools.h"
#include "core/Settings.h"
#include "core/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QUrl>
#include <QtMath>
#include <cmath>

namespace kl {

bool ClipFilter::matches(const ClipItem &c) const {
    if (!kinds.isEmpty() && !kinds.contains(c.kind)) return false;
    if (!game.isEmpty() && c.game != game) return false;
    if (favoritesOnly && !c.favorite) return false;
    if (highlightsOnly && !c.highlight) return false;
    if (!search.isEmpty() && !c.displayTitle().contains(search, Qt::CaseInsensitive) && !c.game.contains(search, Qt::CaseInsensitive)) return false;
    return true;
}

ClipModel::ClipModel(QObject *parent) : QAbstractListModel(parent) {
    auto &lib = ClipLibrary::instance();
    connect(&lib, &ClipLibrary::changed, this, &ClipModel::reload);
    connect(&lib, &ClipLibrary::thumbnailReady, this, [this](const QString &path) {
        for (int i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].path == path) Q_EMIT dataChanged(index(i), index(i));
    });
    reload();
}

void ClipModel::setFilter(const ClipFilter &f) { m_filter = f; reload(); }

void ClipModel::reload() {
    beginResetModel();
    m_rows.clear();
    for (const auto &c : ClipLibrary::instance().items()) {
        if (!m_filter.matches(c)) continue;
        m_rows << c;
        if (m_filter.limit > 0 && m_rows.size() >= m_filter.limit) break;
    }
    endResetModel();
}

QVariant ClipModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid() || idx.row() >= m_rows.size()) return {};
    const ClipItem &c = m_rows[idx.row()];
    if (role == Qt::DisplayRole) return c.displayTitle();
    if (role == PathRole) return c.path;
    if (role == Qt::ToolTipRole) return QStringLiteral("%1\n%2").arg(c.displayTitle(), QDir::toNativeSeparators(c.path));
    return {};
}

static QString sizeText(qint64 b) {
    return QLocale().formattedDataSize(b, 1, QLocale::DataSizeTraditionalFormat);
}

void ClipDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const {
    const auto *model = qobject_cast<const ClipModel *>(idx.model());
    if (!model) return;
    const ClipItem c = model->item(idx.row());
    const auto &pal = theme::current();
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(opt.rect).adjusted(6, 6, -6, -6);
    const bool hover = opt.state & QStyle::State_MouseOver, sel = opt.state & QStyle::State_Selected;
    p->setPen(QPen(sel ? pal.accent : hover ? pal.silver : QColor(pal.silver.red(), pal.silver.green(), pal.silver.blue(), 60), sel ? 1.6 : 1));
    p->setBrush(hover || sel ? pal.raised : pal.panel);
    p->drawRoundedRect(card, 12, 12);

    const QRectF thumb(card.left() + 8, card.top() + 8, card.width() - 16, (card.width() - 16) * 9 / 16);
    QPainterPath clip;
    clip.addRoundedRect(thumb, 8, 8);
    p->setClipPath(clip);
    p->fillRect(thumb, QColor("#060507"));
    const QImage img = ClipLibrary::instance().thumbnail(c.path);
    if (!img.isNull() && img.width() > 1) {
        const QSizeF s = QSizeF(img.size()).scaled(thumb.size(), Qt::KeepAspectRatioByExpanding);
        p->drawImage(QRectF(thumb.center().x() - s.width() / 2, thumb.center().y() - s.height() / 2, s.width(), s.height()), img);
    }
    // gentle vignette so badges stay readable
    QLinearGradient v(thumb.topLeft(), thumb.bottomLeft());
    v.setColorAt(0.55, QColor(0, 0, 0, 0)); v.setColorAt(1, QColor(0, 0, 0, 150));
    p->fillRect(thumb, v);
    p->setClipping(false);

    QFont small(theme::uiFamily(), 8, QFont::Bold);
    p->setFont(small);
    auto badge = [&](const QString &text, const QPointF &at, const QColor &bg, bool right) {
        const QFontMetricsF fm(small);
        const double w = fm.horizontalAdvance(text) + 12;
        const QRectF b(right ? at.x() - w : at.x(), at.y(), w, 18);
        p->setPen(Qt::NoPen);
        p->setBrush(bg);
        p->drawRoundedRect(b, 5, 5);
        p->setPen(Qt::white);
        p->drawText(b, Qt::AlignCenter, text);
    };
    if (c.isVideo() && c.duration > 0) badge(media::formatDuration(c.duration), QPointF(thumb.right() - 6, thumb.bottom() - 24), QColor(0, 0, 0, 170), true);
    const QString kind = c.kind == "recording" ? QObject::tr("SESSION") : c.kind == "trim" ? QObject::tr("TRIM")
                       : c.kind == "screenshot" ? QObject::tr("SCREENSHOT") : QString();
    if (!kind.isEmpty()) badge(kind, QPointF(thumb.left() + 6, thumb.top() + 6), QColor(11, 9, 13, 190), false);
    if (c.highlight) badge(QObject::tr("✦ HIGHLIGHT"), QPointF(thumb.left() + 6, thumb.bottom() - 24), pal.accentDeep, false);
    if (c.favorite) {
        QPainterPath star;
        const QPointF sc(thumb.right() - 16, thumb.top() + 16);
        for (int i = 0; i < 10; ++i) {
            const double r = i % 2 ? 4.0 : 9.0, a = qDegreesToRadians(-90.0 + i * 36);
            const QPointF pt(sc.x() + r * std::cos(a), sc.y() + r * std::sin(a));
            if (i == 0) star.moveTo(pt); else star.lineTo(pt);
        }
        star.closeSubpath();
        p->setPen(QPen(QColor(0, 0, 0, 160), 1));
        p->setBrush(pal.accent);
        p->drawPath(star);
    }
    QFont tf(theme::uiFamily(), 10, QFont::DemiBold);
    p->setFont(tf);
    p->setPen(pal.text);
    const QRectF tr(card.left() + 10, thumb.bottom() + 8, card.width() - 20, 20);
    p->drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(tf).elidedText(c.displayTitle(), Qt::ElideRight, int(tr.width())));
    QFont mf(theme::uiFamily(), 8);
    p->setFont(mf);
    p->setPen(pal.muted);
    const QString meta = QStringLiteral("%1  ·  %2  ·  %3").arg(c.game, QLocale().toString(c.created, QLocale::ShortFormat), sizeText(c.size));
    p->drawText(QRectF(tr.left(), tr.bottom() + 2, tr.width(), 16), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(mf).elidedText(meta, Qt::ElideRight, int(tr.width())));
    p->restore();
}

ClipGrid::ClipGrid(QWidget *parent) : QListView(parent), m_model(new ClipModel(this)) {
    setModel(m_model);
    setItemDelegate(new ClipDelegate(this));
    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setSpacing(2);
    setMouseTracking(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setContextMenuPolicy(Qt::CustomContextMenu);
    viewport()->setAttribute(Qt::WA_Hover);
    connect(this, &QListView::doubleClicked, this, [this](const QModelIndex &i) { Q_EMIT openRequested(i.data(ClipModel::PathRole).toString()); });
    connect(this, &QWidget::customContextMenuRequested, this, &ClipGrid::showMenu);
}

void ClipGrid::paintEvent(QPaintEvent *e) {
    QListView::paintEvent(e);
    if (m_model->rowCount() > 0) return;
    QPainter p(viewport());
    p.setPen(theme::current().muted);
    QFont f(theme::uiFamily(), 10);
    f.setItalic(true);
    p.setFont(f);
    p.drawText(viewport()->rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap,
               m_empty.isEmpty() ? tr("Nothing here yet. Play a game and press %1 to save your first clip.")
                                       .arg(Settings::instance().s("hotkeys.saveClip"))
                                 : m_empty);
}

void ClipGrid::setHorizontalStrip(bool on) {
    setFlow(on ? QListView::LeftToRight : QListView::LeftToRight);
    setWrapping(!on);
    setHorizontalScrollBarPolicy(on ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(on ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    if (on) setFixedHeight(240);
}

void ClipGrid::showMenu(const QPoint &pos) {
    const QModelIndex i = indexAt(pos);
    if (!i.isValid()) return;
    const QString path = i.data(ClipModel::PathRole).toString();
    const ClipItem *c = ClipLibrary::instance().find(path);
    if (!c) return;
    QMenu m(this);
    m.addAction(c->isVideo() ? tr("Open in editor") : tr("Open"), this, [this, path] { Q_EMIT openRequested(path); });
    m.addAction(tr("Copy clip (paste into Discord or a folder)"), this, [path] { clipactions::copyToClipboard(path); });
    m.addAction(c->favorite ? tr("Remove from favorites") : tr("Add to favorites"), this, [path, fav = c->favorite] {
        ClipLibrary::instance().setFavorite(path, !fav);
    });
    m.addAction(tr("Rename..."), this, [this, path] { clipactions::rename(this, path); });
    m.addAction(tr("Show in folder"), this, [path] { clipactions::showInFolder(path); });
    m.addSeparator();
    m.addAction(tr("Delete"), this, [this, path] { clipactions::remove(this, path); });
    m.exec(viewport()->mapToGlobal(pos));
}

namespace clipactions {

void copyToClipboard(const QString &path) {
    auto *mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(path)});
    QApplication::clipboard()->setMimeData(mime);
}

void showInFolder(const QString &path) {
#ifdef _WIN32
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

bool rename(QWidget *parent, const QString &path) {
    const ClipItem *c = ClipLibrary::instance().find(path);
    bool ok = false;
    const QString t = QInputDialog::getText(parent, QObject::tr("Rename"), QObject::tr("Clip name:"), QLineEdit::Normal,
                                            c ? c->displayTitle() : QFileInfo(path).completeBaseName(), &ok);
    if (!ok || t.trimmed().isEmpty()) return false;
    ClipLibrary::instance().setTitle(path, t);
    return true;
}

bool remove(QWidget *parent, const QString &path) {
    if (QMessageBox::question(parent, QObject::tr("Delete"), QObject::tr("Move \"%1\" to the Recycle Bin?").arg(QFileInfo(path).fileName()))
        != QMessageBox::Yes)
        return false;
    QString err;
    if (!ClipLibrary::instance().remove(path, &err)) { QMessageBox::warning(parent, QObject::tr("Delete"), err); return false; }
    return true;
}

} // namespace clipactions
} // namespace kl
