#pragma once

namespace kl::autostart {
bool isEnabled();
void setEnabled(bool on);   // HKCU\...\Run, starts KlipIt quietly in the tray
}
