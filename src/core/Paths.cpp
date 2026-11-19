#include "core/Paths.h"
#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>

namespace kl {

QString Paths::ensure(const QString &dir) {
    QDir().mkpath(dir);
    return dir;
}

QString Paths::dataRoot() {
#ifdef _WIN32
    const QString base = qEnvironmentVariable("APPDATA", QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
#else
    const QString base = qEnvironmentVariable("KLIPIT_DATA", QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
#endif
    return ensure(base + "/KlipIt");
}

QString Paths::configFile() { return dataRoot() + "/config.json"; }
QString Paths::logs()       { return ensure(dataRoot() + "/logs"); }
QString Paths::thumbnails() { return ensure(dataRoot() + "/thumbs"); }

QString Paths::defaultClips() {
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/KlipIt";
}

QString safeFileName(const QString &s) {
    QString out = s;
    out.replace(QRegularExpression(R"([<>:"/\\|?*\x00-\x1F])"), " ");
    out = out.simplified();
    while (out.endsWith('.')) out.chop(1);
    if (out.size() > 60) out = out.left(60).trimmed();
    return out.isEmpty() ? QStringLiteral("Clip") : out;
}

} // namespace kl
