#pragma once
#include <QString>

namespace kl {

// User data lives in %APPDATA%\KlipIt (never inside the install folder), so
// reinstalling or upgrading never touches settings or clips.
struct Paths {
    static QString dataRoot();       // %APPDATA%/KlipIt
    static QString configFile();     // config.json
    static QString logs();           // logs/
    static QString thumbnails();     // thumbs/
    static QString defaultClips();   // Videos/KlipIt
    static QString ensure(const QString &dir);
};

// Makes a string safe to use as a file or folder name.
QString safeFileName(const QString &s);

} // namespace kl
