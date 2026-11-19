#include "core/Logger.h"
#include "core/Paths.h"
#include "BuildInfo.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <cstdarg>
#include <cstdio>
#include <obs.h>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

namespace kl {

static QFile  *g_file = nullptr;
static QMutex  g_mutex;
static QString g_path;

static void writeLine(const char *level, const QString &msg) {
    QMutexLocker lock(&g_mutex);
    const QString line = QStringLiteral("%1 [%2] %3\n")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), QLatin1String(level), msg);
    if (g_file) { g_file->write(line.toUtf8()); g_file->flush(); }
    fputs(line.toUtf8().constData(), stderr);
}

static void qtHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    const char *lvl = type == QtWarningMsg ? "warn" : type == QtCriticalMsg ? "error"
                    : type == QtFatalMsg ? "FATAL" : type == QtDebugMsg ? "debug" : "info";
    writeLine(lvl, msg);
}

static void obsHandler(int lvl, const char *fmt, va_list args, void *) {
#ifdef NDEBUG
    if (lvl == LOG_DEBUG) return;
#endif
    char buf[8192];
    vsnprintf(buf, sizeof(buf), fmt, args);
    writeLine(lvl == LOG_ERROR ? "obs:error" : lvl == LOG_WARNING ? "obs:warn" : "obs", QString::fromUtf8(buf));
}

#ifdef _WIN32
static LONG WINAPI crashFilter(EXCEPTION_POINTERS *info) {
    const QString name = Paths::logs() + "/crash_" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".dmp";
    HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(name.utf16()), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), info, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, MiniDumpWithIndirectlyReferencedMemory, &mei, nullptr, nullptr);
        CloseHandle(h);
    }
    writeLine("FATAL", QStringLiteral("Crash 0x%1, dump: %2").arg(info->ExceptionRecord->ExceptionCode, 8, 16, QChar('0')).arg(name));
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void Logger::init() {
    QDir dir(Paths::logs());
    const auto files = dir.entryInfoList({"*.txt"}, QDir::Files, QDir::Time);
    for (int i = 9; i < files.size(); ++i) QFile::remove(files[i].absoluteFilePath());
    g_path = dir.filePath(QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".txt");
    g_file = new QFile(g_path);
    if (!g_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) { delete g_file; g_file = nullptr; }
    qInstallMessageHandler(qtHandler);
#ifdef _WIN32
    SetUnhandledExceptionFilter(crashFilter);
#endif
    qInfo("%s %s starting", KL_APP_NAME, KL_APP_VERSION);
}

void Logger::installObsLogHandler() { base_set_log_handler(obsHandler, nullptr); }
QString Logger::currentLogFile() { return g_path; }

void Logger::shutdown() {
    QMutexLocker lock(&g_mutex);
    if (g_file) { g_file->close(); delete g_file; g_file = nullptr; }
}

} // namespace kl
