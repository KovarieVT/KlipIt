#include "core/Autostart.h"
#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace kl::autostart {

#ifdef _WIN32
static QSettings runKey() { return QSettings(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)", QSettings::NativeFormat); }
bool isEnabled() { return runKey().contains("KlipIt"); }
void setEnabled(bool on) {
    QSettings k(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)", QSettings::NativeFormat);
    if (on) k.setValue("KlipIt", QStringLiteral("\"%1\" --tray").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
    else k.remove("KlipIt");
}
#else
bool isEnabled() { return false; }
void setEnabled(bool) {}
#endif

} // namespace kl::autostart
