#pragma once
#include "core/ClipLibrary.h"
#include <QAbstractListModel>
#include <QListView>
#include <QSet>
#include <QStyledItemDelegate>

namespace kl {

struct ClipFilter {
    QSet<QString> kinds;            // empty = everything
    QString game;                   // empty = all games
    bool favoritesOnly = false, highlightsOnly = false;
    QString search;
    int limit = 0;                  // 0 = no limit
    bool matches(const ClipItem &c) const;
};

class ClipModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { PathRole = Qt::UserRole + 1 };
    explicit ClipModel(QObject *parent = nullptr);
    void setFilter(const ClipFilter &f);
    ClipFilter filter() const { return m_filter; }
    int rowCount(const QModelIndex & = {}) const override { return int(m_rows.size()); }
    QVariant data(const QModelIndex &idx, int role) const override;
    ClipItem item(int row) const { return m_rows.value(row); }
    void reload();

private:
    ClipFilter m_filter;
    QVector<ClipItem> m_rows;
};

class ClipDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return {286, 222}; }
};

// Grid of clip cards. Double-click opens a clip in the player/editor;
// right-click gives favourite, rename, show in folder, copy, delete.
class ClipGrid : public QListView {
    Q_OBJECT
public:
    explicit ClipGrid(QWidget *parent = nullptr);
    ClipModel *clipModel() const { return m_model; }
    void setFilter(const ClipFilter &f) { m_model->setFilter(f); }
    void setHorizontalStrip(bool on);   // single scrolling row (Home page)
    void setEmptyText(const QString &t) { m_empty = t; }

protected:
    void paintEvent(QPaintEvent *e) override;

Q_SIGNALS:
    void openRequested(const QString &path);

private:
    void showMenu(const QPoint &pos);
    ClipModel *m_model;
    QString m_empty;
};

// Shared clip actions (also used by the player)
namespace clipactions {
void copyToClipboard(const QString &path);        // paste straight into Discord, a chat or a folder
void showInFolder(const QString &path);
bool rename(QWidget *parent, const QString &path);
bool remove(QWidget *parent, const QString &path);
}

} // namespace kl
