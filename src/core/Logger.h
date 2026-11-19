#pragma once
#include <QString>

namespace kl {

// Writes Qt and libobs messages to %APPDATA%\KlipIt\logs (newest 10 kept).
struct Logger {
    static void init();
    static void installObsLogHandler();
    static QString currentLogFile();
    static void shutdown();
};

} // namespace kl
