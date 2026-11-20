#include "core/WebUploader.h"
#include "core/Account.h"
#include "core/ClipLibrary.h"
#include "core/MediaTools.h"
#include "core/Paths.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QUuid>

namespace kl {

WebUploader &WebUploader::instance() {
    static WebUploader u;
    return u;
}

QString WebUploader::send(const QString &path) {
    if (!Account::instance().signedIn()) return tr("Sign in to your KlipIt account first (Account in the sidebar).");
    const QFileInfo fi(path);
    if (!fi.exists() || fi.size() == 0) return tr("The clip's file is missing.");
    if (fi.suffix().compare("mp4", Qt::CaseInsensitive) != 0) return tr("Only MP4 videos can be sent to the website.");
    if (fi.size() > kMaxBytes) return tr("This video is over 4 GB, the most the website takes per video. Trim it in the Video Editor first.");
    if (isQueued(path)) return {};
    m_queue << path;
    Q_EMIT queueChanged();
    if (!isBusy()) QTimer::singleShot(0, this, &WebUploader::next);
    return {};
}

void WebUploader::next() {
    if (isBusy() || m_queue.isEmpty()) return;
    m_current = m_queue.takeFirst();
    const QFileInfo fi(m_current);
    if (const ClipItem *c = ClipLibrary::instance().find(m_current)) {
        m_title = c->displayTitle();
        m_game = c->game;
        m_duration = c->duration;
        m_recorded = c->created;
    } else {
        m_title = fi.completeBaseName();
        m_game.clear();
        m_duration = 0;
        m_recorded = fi.lastModified();
    }
    if (m_duration <= 0) m_duration = media::probe(m_current).duration;
    if (!m_recorded.isValid()) m_recorded = QDateTime::currentDateTime();

    // the thumbnail the website shows: the last moments of the clip, like the app's own cards
    m_thumbFile.clear();
    const QImage thumb = media::thumbnail(m_current, m_duration > 4 ? m_duration - 2 : m_duration / 2, 640);
    if (!thumb.isNull()) {
        const QString f = Paths::thumbnails() + "/upload-" + QUuid::createUuid().toString(QUuid::Id128) + ".jpg";
        if (thumb.save(f, "JPG", 86)) m_thumbFile = f;
    }
    Q_EMIT queueChanged();
    Q_EMIT progress(m_current, 0);

    Account::instance().withToken([this](const QString &token, const QString &err) {
        if (!err.isEmpty()) { finish(err); return; }
        uploadFile(token, m_current, "video/mp4", QFileInfo(m_current).fileName(), true, [this](const QString &videoUrl, const QString &err) {
            if (!err.isEmpty()) { finish(err); return; }
            // a long upload can outlast the sign-in token, so ask for a fresh one
            Account::instance().withToken([this, videoUrl](const QString &token, const QString &err) {
                if (!err.isEmpty()) { finish(err); return; }
                if (m_thumbFile.isEmpty()) { addToClips(token, videoUrl, {}); return; }
                uploadFile(token, m_thumbFile, "image/jpeg", QFileInfo(m_current).completeBaseName() + ".jpg", false,
                           [this, token, videoUrl](const QString &thumbUrl, const QString &) {
                               addToClips(token, videoUrl, thumbUrl);   // a clip without a thumbnail still counts
                           });
            });
        });
    });
}

void WebUploader::uploadFile(const QString &token, const QString &file, const QByteArray &mime, const QString &name, bool reportProgress,
                             std::function<void(const QString &, const QString &)> done) {
    // 1. a one-time upload link for this member (meant for content site members create)
    QNetworkReply *r = Account::instance().postJson("/ricos/v1/ricos-document/generate-file-upload-url",
                                                    QJsonObject{{"mimeType", QString::fromLatin1(mime)}, {"fileName", name}, {"private", false}}, token);
    connect(r, &QNetworkReply::finished, this, [this, r, file, mime, reportProgress, done] {
        r->deleteLater();
        const QByteArray body = r->readAll();
        const QString uploadUrl = QJsonDocument::fromJson(body).object().value("uploadUrl").toString();
        if (r->error() != QNetworkReply::NoError || uploadUrl.isEmpty()) { done({}, Account::errorText(r, body)); return; }

        // 2. the file itself, streamed from disk
        auto *f = new QFile(file);
        if (!f->open(QIODevice::ReadOnly)) { delete f; done({}, tr("KlipIt couldn't read the clip file.")); return; }
        QNetworkRequest rq{QUrl(uploadUrl)};
        rq.setHeader(QNetworkRequest::ContentTypeHeader, mime);
        rq.setHeader(QNetworkRequest::ContentLengthHeader, f->size());
        rq.setTransferTimeout(120000);   // gives up only after 2 minutes without progress
        QNetworkReply *up = Account::instance().net()->put(rq, f);
        f->setParent(up);
        if (reportProgress) {
            const QString path = m_current;
            connect(up, &QNetworkReply::uploadProgress, this, [this, path](qint64 sent, qint64 total) {
                if (total > 0) Q_EMIT progress(path, int(sent * 97 / total));
            });
        }
        connect(up, &QNetworkReply::finished, this, [up, done] {
            up->deleteLater();
            const QByteArray b = up->readAll();
            const QString url = QJsonDocument::fromJson(b).object().value("file").toObject().value("url").toString();
            if (up->error() != QNetworkReply::NoError || url.isEmpty()) {
                const int status = up->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                done({}, status == 413 ? tr("The website's media storage is full or the file is too big. Free up space in Wix or upgrade the site's plan.")
                                       : Account::errorText(up, b));
                return;
            }
            done(url, {});
        });
    });
}

void WebUploader::addToClips(const QString &token, const QString &videoUrl, const QString &thumbUrl) {
    QJsonObject data{
        {"title", m_title},
        {"game", m_game},
        {"videoUrl", videoUrl},
        {"durationSec", qRound(m_duration)},
        {"recordedAt", QJsonObject{{"$date", m_recorded.toUTC().toString(Qt::ISODateWithMs)}}},
    };
    if (!thumbUrl.isEmpty()) data["thumbnail"] = thumbUrl;
    // 3. the private entry in the member's Clips list (only they can read it)
    QNetworkReply *r = Account::instance().postJson(
        "/wix-data/v2/items", QJsonObject{{"dataCollectionId", "Clips"}, {"dataItem", QJsonObject{{"data", data}}}}, token);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const QByteArray body = r->readAll();
        const QString id = QJsonDocument::fromJson(body).object().value("dataItem").toObject().value("id").toString();
        if (r->error() != QNetworkReply::NoError || id.isEmpty()) { finish(Account::errorText(r, body)); return; }
        ClipLibrary::instance().setWebId(m_current, id);
        finish({});
    });
}

void WebUploader::finish(const QString &error) {
    if (!m_thumbFile.isEmpty()) QFile::remove(m_thumbFile);
    m_thumbFile.clear();
    const QString path = std::exchange(m_current, QString());
    if (error.isEmpty()) {
        Q_EMIT progress(path, 100);
        Q_EMIT sent(path, m_title);
    } else {
        qWarning("Sending %s to the website failed: %s", qPrintable(path), qPrintable(error));
        Q_EMIT failed(path, m_title, error);
    }
    Q_EMIT queueChanged();
    QTimer::singleShot(0, this, &WebUploader::next);
}

} // namespace kl
