#pragma once
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariant>

namespace kl {

// JSON settings with defaults and dotted keys ("capture.fps"). Saved atomically.
class Settings : public QObject {
    Q_OBJECT
public:
    static Settings &instance();
    static QJsonObject defaults();

    bool load(const QString &path);
    bool save() const;
    void resetToDefaults();

    QVariant get(const QString &key) const;
    void set(const QString &key, const QVariant &value);

    int     i(const QString &k) const { return get(k).toInt(); }
    bool    b(const QString &k) const { return get(k).toBool(); }
    QString s(const QString &k) const { return get(k).toString(); }
    QStringList list(const QString &k) const { return get(k).toStringList(); }

Q_SIGNALS:
    void changed(const QString &key);

private:
    Settings() = default;
    QJsonObject m_root;
    QString m_path;
};

} // namespace kl
