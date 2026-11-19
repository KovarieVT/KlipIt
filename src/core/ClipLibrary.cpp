#include "core/ClipLibrary.h"
#include "core/MediaTools.h"
#include "core/Paths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtConcurrent>
#include <algorithm>

namespace kl {

QString ClipItem::displayTitle() const {
    if (!title.isEmpty()) return title;
    return QFileInfo(path).completeBaseName();
}

ClipLibrary &ClipLibrary::instance() {
    static ClipLibrary l;
    return l;
}

static QString metaPath() { return Paths::dataRoot() + "/library.json"; }

void ClipLibrary::loadMeta() {
    QFile f(metaPath());
    m_meta = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object().value("items").toObject() : QJsonObject();
}

void ClipLibrary::saveMeta() const {
    QSaveFile f(metaPath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(QJsonObject{{"version", 1}, {"items", m_meta}}).toJson(QJsonDocument::Compact));
    f.commit();
}

void ClipLibrary::setRoot(const QString &folder) {
    m_root = QDir::cleanPath(folder);
    QDir().mkpath(m_root);
    loadMeta();
    rescan();
}

QString ClipLibrary::rel(const QString &path) const { return QDir(m_root).relativeFilePath(path); }

void ClipLibrary::rescan() {
    QVector<ClipItem> out;
    if (m_root.isEmpty()) return;
    QDirIterator it(m_root, {"*.mp4", "*.mov", "*.mkv", "*.png", "*.jpg"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        const QString r = rel(p);
        if (r.startsWith('.') || r.contains("/.")) continue;              // in-progress recordings live in hidden folders
        const QFileInfo fi(p);
        const QJsonObject m = m_meta.value(r).toObject();
        ClipItem c;
        c.path = fi.absoluteFilePath();
        c.relPath = r;
        c.size = fi.size();
        c.created = fi.birthTime().isValid() ? fi.birthTime() : fi.lastModified();
        const QStringList parts = r.split('/');
        c.game = m.value("game").toString(parts.size() > 1 ? parts.first() : QStringLiteral("Desktop"));
        const QString lowRel = r.toLower();
        const bool image = fi.suffix().compare("png", Qt::CaseInsensitive) == 0 || fi.suffix().compare("jpg", Qt::CaseInsensitive) == 0;
        c.kind = m.value("kind").toString(image ? "screenshot"
                                         : lowRel.contains("/sessions/") || lowRel.contains("/recordings/") ? "recording"
                                         : fi.completeBaseName().contains("(trim", Qt::CaseInsensitive) ? "trim" : "clip");
        c.title = m.value("title").toString();
        c.favorite = m.value("favorite").toBool();
        c.highlight = m.value("highlight").toBool();
        if (qint64(m.value("size").toDouble()) == c.size) {
            c.duration = m.value("duration").toDouble();
            c.width = m.value("w").toInt();
            c.height = m.value("h").toInt();
        } else if (!image) {
            probeLater(c.path);
        }
        out << c;
    }
    std::sort(out.begin(), out.end(), [](const ClipItem &a, const ClipItem &b) { return a.created > b.created; });
    m_items = out;
    Q_EMIT changed();
}

void ClipLibrary::probeLater(const QString &path) {
    if (m_pending.contains(path)) return;
    m_pending.insert(path);
    (void)QtConcurrent::run([this, path] {
        const media::Info info = media::probe(path);
        const qint64 size = QFileInfo(path).size();
        QMetaObject::invokeMethod(this, [this, path, info, size] {
            m_pending.remove(path);
            const QString r = rel(path);
            QJsonObject m = m_meta.value(r).toObject();
            m["duration"] = info.duration;
            m["w"] = info.width;
            m["h"] = info.height;
            m["size"] = double(size);
            m_meta[r] = m;
            saveMeta();
            for (auto &c : m_items)
                if (c.path == path) { c.duration = info.duration; c.width = info.width; c.height = info.height; }
            Q_EMIT changed();
        }, Qt::QueuedConnection);
    });
}

const ClipItem *ClipLibrary::find(const QString &path) const {
    for (const auto &c : m_items) if (c.path == path) return &c;
    return nullptr;
}

QStringList ClipLibrary::games() const {
    QHash<QString, int> n;
    for (const auto &c : m_items) ++n[c.game];
    QStringList g = n.keys();
    std::sort(g.begin(), g.end(), [&](const QString &a, const QString &b) { return n[a] != n[b] ? n[a] > n[b] : a < b; });
    return g;
}

int ClipLibrary::countForGame(const QString &game) const {
    return int(std::count_if(m_items.begin(), m_items.end(), [&](const ClipItem &c) { return c.game == game; }));
}

qint64 ClipLibrary::totalBytes() const {
    qint64 t = 0;
    for (const auto &c : m_items) t += c.size;
    return t;
}

QString ClipLibrary::folderFor(const QString &game, const QString &kind) const {
    QString d = m_root + "/" + safeFileName(game.isEmpty() ? QStringLiteral("Desktop") : game);
    if (kind == "screenshot") d += "/Screenshots";
    else if (kind == "recording") d += "/Sessions";
    return Paths::ensure(d);
}

void ClipLibrary::add(const QString &path, const QString &game, const QString &kind, bool highlight, const QString &title) {
    const QString r = rel(path);
    QJsonObject m = m_meta.value(r).toObject();
    m["game"] = game.isEmpty() ? QStringLiteral("Desktop") : game;
    m["kind"] = kind;
    if (highlight) m["highlight"] = true;
    if (!title.isEmpty()) m["title"] = title;
    m_meta[r] = m;
    saveMeta();
    rescan();
}

void ClipLibrary::setFavorite(const QString &path, bool on) {
    const QString r = rel(path);
    QJsonObject m = m_meta.value(r).toObject();
    m["favorite"] = on;
    m_meta[r] = m;
    saveMeta();
    for (auto &c : m_items) if (c.path == path) c.favorite = on;
    Q_EMIT changed();
}

void ClipLibrary::setTitle(const QString &path, const QString &title) {
    const QString r = rel(path);
    QJsonObject m = m_meta.value(r).toObject();
    m["title"] = title.trimmed();
    m_meta[r] = m;
    saveMeta();
    for (auto &c : m_items) if (c.path == path) c.title = title.trimmed();
    Q_EMIT changed();
}

bool ClipLibrary::remove(const QString &path, QString *error) {
    if (!QFile::moveToTrash(path) && !QFile::remove(path)) {
        if (error) *error = tr("Couldn't delete the file. Is it open in another program?");
        return false;
    }
    m_meta.remove(rel(path));
    saveMeta();
    m_thumbs.remove(path);
    rescan();
    return true;
}

QImage ClipLibrary::thumbnail(const QString &path) {
    if (auto it = m_thumbs.constFind(path); it != m_thumbs.cend()) return *it;
    const QFileInfo fi(path);
    const QString key = QString::fromLatin1(QCryptographicHash::hash((path + QString::number(fi.size()) + fi.lastModified().toString(Qt::ISODate)).toUtf8(),
                                                                     QCryptographicHash::Md5).toHex());
    const QString cache = Paths::thumbnails() + "/" + key + ".jpg";
    QImage img;
    if (img.load(cache)) { m_thumbs.insert(path, img); return img; }
    if (m_pending.contains("thumb:" + path)) return {};
    m_pending.insert("thumb:" + path);
    const bool image = fi.suffix().compare("png", Qt::CaseInsensitive) == 0 || fi.suffix().compare("jpg", Qt::CaseInsensitive) == 0;
    (void)QtConcurrent::run([this, path, cache, image] {
        QImage t = image ? QImage(path).scaledToWidth(480, Qt::SmoothTransformation) : media::thumbnail(path, 2.0, 480);
        if (!t.isNull()) t.save(cache, "JPG", 82);
        QMetaObject::invokeMethod(this, [this, path, t] {
            m_pending.remove("thumb:" + path);
            m_thumbs.insert(path, t.isNull() ? QImage(1, 1, QImage::Format_RGB32) : t);
            Q_EMIT thumbnailReady(path);
        }, Qt::QueuedConnection);
    });
    return {};
}

} // namespace kl
