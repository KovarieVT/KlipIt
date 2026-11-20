#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTimer;

namespace kl {

// Your KlipIt website account (a Wix member account).
//
// Sign in opens the website's login page in your browser, so the site's own security check and
// sign-up work as usual and KlipIt never sees your password. When you've logged in there, the
// page sends a one-time code back to KlipIt on 127.0.0.1. KlipIt swaps that code (plus a secret
// only it knows, PKCE) for its own member sign-in. The long-lived part is saved encrypted for your
// Windows user in %APPDATA%\KlipIt\account.dat.
class Account : public QObject {
    Q_OBJECT
public:
    static Account &instance();
    static QString siteUrl();    // the KlipIt website (KLIPIT_SITE overrides it for tests)
    static QString apiUrl();     // Wix's API (KLIPIT_API overrides it for tests)
    static QString clientId();

    bool signedIn() const { return !m_refresh.isEmpty(); }
    bool signingIn() const { return m_server != nullptr; }
    QString name() const { return m_name; }
    QString email() const { return m_email; }
    QString lastError() const { return m_error; }

    void load();                       // reads the saved sign-in (call once at startup)
    void signIn(bool createAccount = false);
    void cancelSignIn();
    void signOut();

    // Calls back with a member access token (refreshed when needed) or an error message.
    using TokenCallback = std::function<void(const QString &token, const QString &error)>;
    void withToken(TokenCallback cb);

    QNetworkAccessManager *net();
    QNetworkReply *postJson(const QString &path, const QJsonObject &body, const QString &token = {});
    static QString errorText(QNetworkReply *r, const QByteArray &body);

    // PKCE (RFC 7636): random verifier and its S256 challenge.
    static QString randomToken();
    static QString challengeFor(const QString &verifier);

    // Tests replace the browser with this.
    std::function<void(const QUrl &)> openBrowser;

Q_SIGNALS:
    void changed();
    void signInFinished(bool ok, const QString &message);

private:
    Account() = default;
    void onConnection();
    void exchange(const QString &code);
    void fetchProfile();
    void mint(const QJsonObject &body, std::function<void(const QString &error)> done);
    void save() const;
    void stopServer();
    void fail(const QString &message);
    QString storePath() const;

    QString m_access, m_refresh, m_name, m_email, m_error;
    QDateTime m_expires;
    QString m_verifier, m_state;
    QTcpServer *m_server = nullptr;
    QTimer *m_timeout = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    QList<TokenCallback> m_waiting;
    bool m_refreshing = false;
};

} // namespace kl
