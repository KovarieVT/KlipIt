#pragma once
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QVector>

namespace kl {

struct ClipItem {
    QString path, relPath;
    QString game;          // folder / detected game ("Desktop" when not from a game)
    QString kind;          // clip | recording | trim | screenshot
    QString title;
    QDateTime created;
    qint64 size = 0;
    double duration = 0;   // seconds (0 for screenshots or not probed yet)
    int width = 0, height = 0;
    bool favorite = false, highlight = false;
    bool isVideo() const { return kind != "screenshot"; }
    QString displayTitle() const;
};

// Everything KlipIt has saved, organised by game. The clip files live in the
// clips folder (one subfolder per game); titles, favourites and cached facts
// live in %APPDATA%\KlipIt\library.json.
class ClipLibrary : public QObject {
    Q_OBJECT
public:
    static ClipLibrary &instance();
    void setRoot(const QString &folder);
    QString root() const { return m_root; }
    void rescan();

    QVector<ClipItem> items() const { return m_items; }
    const ClipItem *find(const QString &path) const;
    QStringList games() const;                       // most clips first
    int countForGame(const QString &game) const;
    qint64 totalBytes() const;

    // Folder new files for this game go into (created on demand).
    QString folderFor(const QString &game, const QString &kind) const;
    // Registers a file KlipIt just wrote.
    void add(const QString &path, const QString &game, const QString &kind, bool highlight = false, const QString &title = {});
    void setFavorite(const QString &path, bool on);
    void setTitle(const QString &path, const QString &title);
    bool remove(const QString &path, QString *error = nullptr);   // moves to the Recycle Bin when possible

    QImage thumbnail(const QString &path);           // cached; empty until ready (then thumbnailReady fires)

Q_SIGNALS:
    void changed();
    void thumbnailReady(const QString &path);

private:
    ClipLibrary() = default;
    void loadMeta();
    void saveMeta() const;
    QString rel(const QString &path) const;
    void probeLater(const QString &path);
    QString m_root;
    QVector<ClipItem> m_items;
    QJsonObject m_meta;                              // relPath -> {game, kind, title, favorite, highlight, duration, w, h, size}
    QHash<QString, QImage> m_thumbs;
    QSet<QString> m_pending;
};

} // namespace kl
