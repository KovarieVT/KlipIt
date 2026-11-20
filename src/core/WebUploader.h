#pragma once
#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <functional>

class QNetworkReply;

namespace kl {

// Sends clips to "My clips" on the KlipIt website, one at a time.
// For each clip: Wix gives a one-time upload link, the video goes straight to the site's media
// storage, a thumbnail follows, and a private entry is added to the member's Clips list (only the
// signed-in member can see it). Nothing goes anywhere unless you're signed in.
class WebUploader : public QObject {
    Q_OBJECT
public:
    static WebUploader &instance();
    static constexpr qint64 kMaxBytes = 4LL * 1024 * 1024 * 1024;   // Wix's limit per video

    // Queues a clip. Returns an empty string when queued, else why it can't be sent.
    QString send(const QString &path);
    bool isBusy() const { return !m_current.isEmpty(); }
    bool isQueued(const QString &path) const { return m_current == path || m_queue.contains(path); }
    QString current() const { return m_current; }
    int pending() const { return int(m_queue.size()) + (isBusy() ? 1 : 0); }

Q_SIGNALS:
    void queueChanged();
    void progress(const QString &path, int percent);
    void sent(const QString &path, const QString &title);
    void failed(const QString &path, const QString &title, const QString &message);

private:
    WebUploader() = default;
    void next();
    void finish(const QString &error);
    // Uploads one file to the site's media; calls back with its public URL or an error.
    void uploadFile(const QString &token, const QString &file, const QByteArray &mime, const QString &name, bool reportProgress,
                    std::function<void(const QString &url, const QString &error)> done);
    void addToClips(const QString &token, const QString &videoUrl, const QString &thumbUrl);

    QStringList m_queue;
    QString m_current, m_title, m_game, m_thumbFile;
    double m_duration = 0;
    QDateTime m_recorded;
};

} // namespace kl
