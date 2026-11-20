#include "core/Account.h"
#include "core/Paths.h"
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#endif

namespace kl {

// ------------------------------------------------------------------ settings
QString Account::siteUrl() {
    return qEnvironmentVariable("KLIPIT_SITE", "https://headless-qxoilgnxgbe-jaesolas8-1409.wix-site-host.com");
}
QString Account::apiUrl() { return qEnvironmentVariable("KLIPIT_API", "https://www.wixapis.com"); }
QString Account::clientId() { return QStringLiteral("e7e466ab-25cd-4038-a670-f86651b2b02b"); }   // public, not a secret

Account &Account::instance() {
    static Account a;
    return a;
}

QNetworkAccessManager *Account::net() {
    if (!m_net) m_net = new QNetworkAccessManager(this);
    return m_net;
}

// ------------------------------------------------------------------ PKCE
static QString base64url(const QByteArray &b) {
    return QString::fromLatin1(b.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString Account::randomToken() {
    QByteArray b(32, 0);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(b.data()), b.size() / 4);
    return base64url(b);
}

QString Account::challengeFor(const QString &verifier) {
    return base64url(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256));
}

// ------------------------------------------------------------------ saved sign-in (encrypted per Windows user)
static QByteArray protect(const QByteArray &in) {
#ifdef _WIN32
    DATA_BLOB a{DWORD(in.size()), reinterpret_cast<BYTE *>(const_cast<char *>(in.data()))}, out{};
    if (!CryptProtectData(&a, L"KlipIt sign-in", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    QByteArray r(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return r;
#else
    return in;
#endif
}

static QByteArray unprotect(const QByteArray &in) {
#ifdef _WIN32
    DATA_BLOB a{DWORD(in.size()), reinterpret_cast<BYTE *>(const_cast<char *>(in.data()))}, out{};
    if (!CryptUnprotectData(&a, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    QByteArray r(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return r;
#else
    return in;
#endif
}

QString Account::storePath() const { return Paths::dataRoot() + "/account.dat"; }

void Account::load() {
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray raw = f.readAll();
    if (!raw.startsWith("KLA1")) return;
    const QJsonObject o = QJsonDocument::fromJson(unprotect(raw.mid(4))).object();
    m_refresh = o.value("refresh").toString();
    m_name = o.value("name").toString();
    m_email = o.value("email").toString();
    Q_EMIT changed();
}

void Account::save() const {
    if (!signedIn()) { QFile::remove(storePath()); return; }
    const QByteArray json = QJsonDocument(QJsonObject{{"refresh", m_refresh}, {"name", m_name}, {"email", m_email}}).toJson(QJsonDocument::Compact);
    const QByteArray enc = protect(json);
    if (enc.isEmpty()) return;
    QSaveFile f(storePath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write("KLA1" + enc);
    f.commit();
}

// ------------------------------------------------------------------ HTTP helpers
QNetworkReply *Account::postJson(const QString &path, const QJsonObject &body, const QString &token) {
    QNetworkRequest rq(QUrl(apiUrl() + path));
    rq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!token.isEmpty()) rq.setRawHeader("Authorization", token.toUtf8());   // Wix wants the bare token
    rq.setTransferTimeout(60000);
    return net()->post(rq, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

QString Account::errorText(QNetworkReply *r, const QByteArray &body) {
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString msg = QJsonDocument::fromJson(body).object().value("message").toString();
    if (status == 0) return tr("Couldn't reach the KlipIt website (%1). Check your internet connection.").arg(r->errorString());
    if (!msg.isEmpty()) return QStringLiteral("%1 (%2)").arg(msg).arg(status);
    return tr("The website answered with error %1.").arg(status);
}

void Account::mint(const QJsonObject &body, std::function<void(const QString &)> done) {
    QJsonObject b = body;
    b["clientId"] = clientId();
    QNetworkReply *r = postJson("/oauth2/token", b);
    connect(r, &QNetworkReply::finished, this, [this, r, done] {
        r->deleteLater();
        const QByteArray data = r->readAll();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(data).object();
        if (r->error() != QNetworkReply::NoError || o.value("access_token").toString().isEmpty()) {
            // 400/401 from the token endpoint means the sign-in itself is no longer valid
            done((status == 400 || status == 401) ? QStringLiteral("invalid_grant") : errorText(r, data));
            return;
        }
        m_access = o.value("access_token").toString();
        const QString refresh = o.value("refresh_token").toString();
        if (!refresh.isEmpty()) m_refresh = refresh;
        m_expires = QDateTime::currentDateTimeUtc().addSecs(qMax(60, o.value("expires_in").toInt(3600)) - 60);
        done({});
    });
}

void Account::withToken(TokenCallback cb) {
    if (!signedIn()) { cb({}, tr("Sign in to your KlipIt account first.")); return; }
    if (!m_access.isEmpty() && QDateTime::currentDateTimeUtc() < m_expires) { cb(m_access, {}); return; }
    m_waiting << cb;
    if (m_refreshing) return;
    m_refreshing = true;
    mint(QJsonObject{{"grantType", "refresh_token"}, {"refreshToken", m_refresh}}, [this](const QString &err) {
        m_refreshing = false;
        const auto waiting = std::exchange(m_waiting, {});
        if (err == "invalid_grant") {
            signOut();
            for (const auto &w : waiting) w({}, tr("Your sign-in expired. Sign in again."));
            return;
        }
        if (!err.isEmpty()) { for (const auto &w : waiting) w({}, err); return; }
        save();   // the refresh token may have been renewed
        for (const auto &w : waiting) w(m_access, {});
    });
}

// ------------------------------------------------------------------ sign in through the browser
static QByteArray page(const QString &title, const QString &text) {
    const QString html = QStringLiteral(
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>KlipIt</title><style>"
        "body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;background:#0B090D;color:#E9E1E5;"
        "font-family:Georgia,'Times New Roman',serif}main{max-width:460px;text-align:center;border:1px solid #451322;border-radius:16px;"
        "padding:40px 48px;background:#151119;box-shadow:0 0 60px rgba(143,25,53,.25)}h1{font-weight:normal;letter-spacing:.06em;"
        "margin:0 0 14px;color:#fff}p{margin:0;color:#B8B2BF;font:15px/1.5 'Segoe UI',Arial,sans-serif}.bar{width:64px;height:2px;"
        "margin:0 auto 22px;background:linear-gradient(90deg,transparent,#B52A48,transparent)}</style></head>"
        "<body><main><div class=\"bar\"></div><h1>%1</h1><p>%2</p></main></body></html>")
                             .arg(title.toHtmlEscaped(), text.toHtmlEscaped());
    return html.toUtf8();
}

static void respond(QTcpSocket *s, int status, const QByteArray &body) {
    if (!s) return;
    const char *reason = status == 200 ? "OK" : status == 404 ? "Not Found" : "Bad Request";
    s->write(QStringLiteral("HTTP/1.1 %1 %2\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %3\r\n"
                            "Cache-Control: no-store\r\nConnection: close\r\n\r\n")
                 .arg(status).arg(QString::fromLatin1(reason)).arg(body.size()).toLatin1());
    s->write(body);
    s->disconnectFromHost();
}

void Account::signIn(bool createAccount) {
    stopServer();
    m_error.clear();
    m_verifier = randomToken();
    m_state = randomToken();
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {   // only reachable from this PC
        delete m_server;
        m_server = nullptr;
        fail(tr("KlipIt couldn't start the sign-in. Try again."));
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this, &Account::onConnection);
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] { fail(tr("Sign-in timed out. Press Sign in to try again.")); });
    m_timeout->start(15 * 60 * 1000);

    QUrl url(siteUrl() + "/login.html");
    QUrlQuery q;
    q.addQueryItem("app", QString::number(m_server->serverPort()));
    q.addQueryItem("state", m_state);
    q.addQueryItem("challenge", challengeFor(m_verifier));
    if (createAccount) q.addQueryItem("mode", "signup");
    url.setQuery(q);
    if (openBrowser) openBrowser(url);
    else QDesktopServices::openUrl(url);
    Q_EMIT changed();
}

void Account::onConnection() {
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *s = m_server->nextPendingConnection();
        s->setParent(this);   // outlives the listener, which closes once the code arrives
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        auto buf = std::make_shared<QByteArray>();   // the request so far
        connect(s, &QTcpSocket::readyRead, this, [this, s, buf] {
            buf->append(s->readAll());
            if (buf->size() > 16384) { s->abort(); return; }
            if (!buf->contains("\r\n\r\n")) return;
            const QList<QByteArray> line = buf->left(buf->indexOf("\r\n")).split(' ');
            buf->clear();
            if (line.size() < 2 || line[0] != "GET") { respond(s, 400, page(tr("Not here"), tr("This address is only for KlipIt sign-in."))); return; }
            const QUrl u(QStringLiteral("http://127.0.0.1") + QString::fromLatin1(line[1]));
            if (u.path() != "/callback") { respond(s, 404, page(tr("Not here"), tr("This address is only for KlipIt sign-in."))); return; }
            const QUrlQuery q(u);
            const QString state = q.queryItemValue("state", QUrl::FullyDecoded);
            const QString code = q.queryItemValue("code", QUrl::FullyDecoded);
            if (!m_server || state.isEmpty() || state != m_state || code.isEmpty()) {
                respond(s, 400, page(tr("Sign-in link expired"), tr("Go back to KlipIt and press Sign in again.")));
                return;
            }
            // the right code: stop listening and finish; the browser gets its answer when we're done
            QPointer<QTcpSocket> sock(s);
            stopServer();
            connect(this, &Account::signInFinished, s, [sock](bool ok, const QString &msg) {
                if (ok) respond(sock, 200, page(tr("You’re signed in"), tr("%1 You can close this tab and go back to KlipIt.").arg(msg)));
                else respond(sock, 200, page(tr("Sign-in didn’t finish"), msg));
            }, Qt::SingleShotConnection);
            exchange(code);
        });
    }
}

void Account::exchange(const QString &code) {
    mint(QJsonObject{{"grantType", "authorization_code"}, {"code", code}, {"codeVerifier", m_verifier}}, [this](const QString &err) {
        m_verifier.clear();
        if (!err.isEmpty()) {
            m_refresh.clear();
            m_access.clear();
            fail(err == "invalid_grant" ? tr("The website didn't accept the sign-in. Press Sign in to try again.") : err);
            return;
        }
        fetchProfile();
    });
}

void Account::fetchProfile() {
    QNetworkRequest rq(QUrl(apiUrl() + "/members/v1/members/my?fieldsets=FULL"));
    rq.setRawHeader("Authorization", m_access.toUtf8());
    rq.setTransferTimeout(30000);
    QNetworkReply *r = net()->get(rq);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const QJsonObject m = QJsonDocument::fromJson(r->readAll()).object().value("member").toObject();
        m_email = m.value("loginEmail").toString();
        m_name = m.value("profile").toObject().value("nickname").toString();
        if (m_name.isEmpty()) m_name = m.value("contact").toObject().value("firstName").toString();
        if (m_name.isEmpty()) m_name = m_email.section('@', 0, 0);
        if (m_name.isEmpty()) m_name = tr("KlipIt member");
        save();
        Q_EMIT changed();
        Q_EMIT signInFinished(true, tr("Signed in as %1.").arg(m_name));
    });
}

void Account::fail(const QString &message) {
    stopServer();
    m_error = message;
    Q_EMIT changed();
    Q_EMIT signInFinished(false, message);
}

void Account::stopServer() {
    if (m_timeout) { m_timeout->deleteLater(); m_timeout = nullptr; }
    if (m_server) { m_server->close(); m_server->deleteLater(); m_server = nullptr; Q_EMIT changed(); }
}

void Account::cancelSignIn() {
    stopServer();
    m_verifier.clear();
    m_state.clear();
}

void Account::signOut() {
    cancelSignIn();
    m_access.clear();
    m_refresh.clear();
    m_name.clear();
    m_email.clear();
    m_expires = {};
    save();   // removes the saved sign-in
    Q_EMIT changed();
}

} // namespace kl
