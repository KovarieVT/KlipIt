// Website sign-in and clip upload, end to end against a stand-in for Wix's API.
// The fake server checks what the real one would: the PKCE verifier matches the challenge the
// browser carried, every call is signed in, the video arrives whole, and the Clips entry is right.
#include "core/Account.h"
#include "core/ClipLibrary.h"
#include "core/WebUploader.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>
#include <memory>

using namespace kl;

class FakeWix : public QObject {
public:
    QTcpServer server;
    QString challenge;                 // what the "browser" carried to the login page
    bool refreshFails = false;
    int expiresIn = 30;                // so short that the next call has to refresh
    QStringList calls;                 // "METHOD /path token"
    QByteArray video, videoType;
    int imageUploads = 0;
    QJsonObject inserted;

    FakeWix() {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                auto buf = std::make_shared<QByteArray>();
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
                connect(s, &QTcpSocket::readyRead, this, [this, s, buf] {
                    buf->append(s->readAll());
                    const int end = buf->indexOf("\r\n\r\n");
                    if (end < 0) return;
                    const QByteArray head = buf->left(end);
                    qsizetype length = 0;
                    for (const QByteArray &h : head.split('\n'))
                        if (h.toLower().startsWith("content-length:")) length = h.mid(15).trimmed().toLongLong();
                    if (buf->size() < end + 4 + length) return;
                    handle(s, head, buf->mid(end + 4, length));
                    buf->clear();
                });
            }
        });
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }

    static QByteArray header(const QByteArray &head, const QByteArray &name) {
        for (const QByteArray &h : head.split('\n'))
            if (h.toLower().startsWith(name.toLower() + ":")) return h.mid(name.size() + 1).trimmed();
        return {};
    }
    static void reply(QTcpSocket *s, int status, const QJsonObject &o) {
        const QByteArray body = QJsonDocument(o).toJson(QJsonDocument::Compact);
        s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                 QByteArray::number(body.size()) + "\r\n\r\n" + body);
        s->disconnectFromHost();
    }

    void handle(QTcpSocket *s, const QByteArray &head, const QByteArray &body) {
        const QList<QByteArray> line = head.left(head.indexOf("\r\n")).split(' ');
        const QString method = line.value(0), target = line.value(1);
        const QString path = QUrl(target).path();
        const QByteArray token = header(head, "Authorization");
        calls << QStringLiteral("%1 %2 %3").arg(method, path, QString::fromLatin1(token));
        const QJsonObject in = QJsonDocument::fromJson(body).object();

        if (method == "POST" && path == "/oauth2/token") {
            if (in.value("clientId").toString() != Account::clientId()) return reply(s, 400, {{"message", "unknown client"}});
            const QString grant = in.value("grantType").toString();
            if (grant == "authorization_code") {
                const QString verifier = in.value("codeVerifier").toString();
                const QString expect = QString::fromLatin1(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)
                                                               .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
                if (in.value("code").toString() != "good-code" || expect != challenge) return reply(s, 400, {{"message", "bad code"}});
                return reply(s, 200, {{"access_token", "acc-1"}, {"refresh_token", "ref-1"}, {"expires_in", expiresIn}});
            }
            if (grant == "refresh_token") {
                if (refreshFails || in.value("refreshToken").toString() != "ref-1") return reply(s, 400, {{"message", "invalid_grant"}});
                return reply(s, 200, {{"access_token", "acc-2"}, {"expires_in", expiresIn}});
            }
            return reply(s, 400, {{"message", "bad grant"}});
        }
        if (method == "PUT" && path.startsWith("/upload/")) {   // the signed upload link needs no sign-in
            if (path == "/upload/video") {
                video = body;
                videoType = header(head, "Content-Type");
                return reply(s, 200, {{"file", QJsonObject{{"id", "vid1"}, {"url", "https://video.wixstatic.com/video/vid1/file"}}}});
            }
            ++imageUploads;
            return reply(s, 200, {{"file", QJsonObject{{"url", "https://static.wixstatic.com/media/img1.jpg"}}}});
        }
        if (token.isEmpty()) return reply(s, 403, {{"message", "not signed in"}});
        if (method == "GET" && path == "/members/v1/members/my")
            return reply(s, 200, {{"member", QJsonObject{{"loginEmail", "tester@example.com"}, {"profile", QJsonObject{{"nickname", "Tester"}}}}}});
        if (method == "POST" && path == "/ricos/v1/ricos-document/generate-file-upload-url")
            return reply(s, 200, {{"uploadUrl", url() + "/upload/" + in.value("mimeType").toString().section('/', 0, 0) + "?sig=abc"}});
        if (method == "POST" && path == "/wix-data/v2/items") {
            inserted = in;
            return reply(s, 200, {{"dataItem", QJsonObject{{"id", "item-1"}, {"dataCollectionId", "Clips"}}}});
        }
        return reply(s, 404, {{"message", "no route"}});
    }
};

class WebTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    FakeWix m_wix;
    QNetworkAccessManager m_browser;
    QUrl m_opened;

    int browserGet(const QString &url, QByteArray *body = nullptr) {
        QNetworkReply *r = m_browser.get(QNetworkRequest(QUrl(url)));
        QSignalSpy done(r, &QNetworkReply::finished);
        if (!done.wait(20000)) return -1;
        if (body) *body = r->readAll();
        r->deleteLater();
        return r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }

private Q_SLOTS:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        qputenv("KLIPIT_DATA", m_dir.path().toUtf8());
        qputenv("APPDATA", m_dir.path().toUtf8());   // Windows keeps KlipIt's data under %APPDATA%
        qputenv("KLIPIT_API", m_wix.url().toUtf8());
        qputenv("KLIPIT_SITE", "https://site.test");
        Account::instance().openBrowser = [this](const QUrl &u) { m_opened = u; };
    }

    void pkceMatchesTheStandard() {
        // RFC 7636, appendix B
        QCOMPARE(Account::challengeFor("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"), QString("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
        const QString a = Account::randomToken(), b = Account::randomToken();
        QCOMPARE(a.size(), 43);
        QVERIFY(a != b);
    }

    void cantSendWhileSignedOut() {
        QVERIFY(!Account::instance().signedIn());
        QVERIFY(!WebUploader::instance().send(m_dir.path() + "/nothing.mp4").isEmpty());
    }

    void signInThroughTheBrowser() {
        auto &a = Account::instance();
        a.signIn();
        QVERIFY(a.signingIn());
        QCOMPARE(m_opened.host(), QString("site.test"));
        QCOMPARE(m_opened.path(), QString("/login.html"));
        const QUrlQuery q(m_opened);
        const int port = q.queryItemValue("app").toInt();
        const QString state = q.queryItemValue("state");
        m_wix.challenge = q.queryItemValue("challenge");
        QVERIFY(port >= 1024);
        QCOMPARE(state.size(), 43);
        QCOMPARE(m_wix.challenge.size(), 43);
        const QString base = QStringLiteral("http://127.0.0.1:%1").arg(port);

        // anything else on that port is refused, and a wrong state doesn't sign in
        QCOMPARE(browserGet(base + "/favicon.ico"), 404);
        QCOMPARE(browserGet(base + "/callback?code=good-code&state=someone-else"), 400);
        QVERIFY(a.signingIn());
        QVERIFY(!a.signedIn());

        // the website hands the code back: KlipIt swaps it for its own sign-in, then answers the browser
        QByteArray page;
        QCOMPARE(browserGet(base + "/callback?code=good-code&state=" + state, &page), 200);
        QVERIFY(page.contains("signed in"));
        QVERIFY(a.signedIn());
        QVERIFY(!a.signingIn());
        QCOMPARE(a.name(), QString("Tester"));
        QCOMPARE(a.email(), QString("tester@example.com"));

        // the port is closed again afterwards
        QCOMPARE(browserGet(base + "/callback?code=good-code&state=" + state), 0);

        // saved for next time, encrypted on Windows
        QFile f(m_dir.path() + "/KlipIt/account.dat");
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray saved = f.readAll();
        QVERIFY(saved.startsWith("KLA1"));
#ifdef _WIN32
        QVERIFY(!saved.contains("ref-1"));
#else
        QVERIFY(saved.contains("ref-1"));
#endif
    }

    void sendsAClipToMyClips() {
        // a clip in the library
        ClipLibrary::instance().setRoot(m_dir.path() + "/clips");
        QDir().mkpath(m_dir.path() + "/clips/Test Game");
        const QString clip = m_dir.path() + "/clips/Test Game/Clutch.mp4";
        QByteArray bytes(700 * 1024, 0);
        for (char &c : bytes) c = char(QRandomGenerator::global()->bounded(256));
        {
            QFile f(clip);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(bytes);
        }
        ClipLibrary::instance().add(clip, "Test Game", "clip", false, "Clutch 1v3");

        auto &up = WebUploader::instance();
        QVERIFY(!up.send(m_dir.path() + "/clips/notes.txt").isEmpty());   // only MP4 videos
        QSignalSpy sent(&up, &WebUploader::sent);
        QSignalSpy failed(&up, &WebUploader::failed);
        QCOMPARE(up.send(clip), QString());
        QVERIFY(up.isQueued(clip));
        QTRY_VERIFY_WITH_TIMEOUT(sent.count() + failed.count() > 0, 30000);
        if (!failed.isEmpty()) QFAIL(qPrintable(failed.first().value(2).toString()));

        // the whole video arrived, as a video
        QCOMPARE(m_wix.video.size(), bytes.size());
        QVERIFY(m_wix.video == bytes);
        QCOMPARE(m_wix.videoType, QByteArray("video/mp4"));
        // the short-lived token was refreshed first, and every call after it used the new one
        QVERIFY(m_wix.calls.contains(QStringLiteral("POST /ricos/v1/ricos-document/generate-file-upload-url acc-2")));
        // the private entry in My clips
        QCOMPARE(m_wix.inserted.value("dataCollectionId").toString(), QString("Clips"));
        const QJsonObject data = m_wix.inserted.value("dataItem").toObject().value("data").toObject();
        QCOMPARE(data.value("title").toString(), QString("Clutch 1v3"));
        QCOMPARE(data.value("game").toString(), QString("Test Game"));
        QVERIFY(data.value("videoUrl").toString().startsWith("https://video.wixstatic.com/video/"));
        QVERIFY(!data.value("recordedAt").toObject().value("$date").toString().isEmpty());
        QCOMPARE(ClipLibrary::instance().find(clip)->webId, QString("item-1"));
        QVERIFY(!up.isQueued(clip));
    }

    void sendsTheThumbnailToo() {
        const QString src = QFINDTESTDATA("data/tiny.mp4");   // a real one-second video
        QVERIFY(!src.isEmpty());
        const QString clip = m_dir.path() + "/clips/Test Game/Tiny.mp4";
        QVERIFY(QFile::copy(src, clip));
        ClipLibrary::instance().add(clip, "Test Game", "clip");
        auto &up = WebUploader::instance();
        QSignalSpy sent(&up, &WebUploader::sent);
        QSignalSpy failed(&up, &WebUploader::failed);
        QCOMPARE(up.send(clip), QString());
        QTRY_VERIFY_WITH_TIMEOUT(sent.count() + failed.count() > 0, 30000);
        if (!failed.isEmpty()) QFAIL(qPrintable(failed.first().value(2).toString()));
        QCOMPARE(m_wix.imageUploads, 1);
        const QJsonObject data = m_wix.inserted.value("dataItem").toObject().value("data").toObject();
        QCOMPARE(data.value("thumbnail").toString(), QString("https://static.wixstatic.com/media/img1.jpg"));
        QCOMPARE(data.value("durationSec").toInt(), 1);
        QCOMPARE(data.value("title").toString(), QString("Tiny"));
    }

    void expiredSignInSignsOut() {
        m_wix.refreshFails = true;
        QString error;
        bool called = false;
        Account::instance().withToken([&](const QString &, const QString &e) { error = e; called = true; });
        QTRY_VERIFY_WITH_TIMEOUT(called, 20000);
        QVERIFY(!error.isEmpty());
        QVERIFY(!Account::instance().signedIn());
        QVERIFY(!QFile::exists(m_dir.path() + "/KlipIt/account.dat"));
    }
};

QTEST_MAIN(WebTest)
#include "test_web.moc"
