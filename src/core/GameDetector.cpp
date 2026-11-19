#include "core/GameDetector.h"
#include "core/Settings.h"
#include <QFileInfo>
#include <QSet>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <iterator>
#endif

namespace kl {

GameDetector &GameDetector::instance() {
    static GameDetector d;
    return d;
}

QString GameDetector::escapeObs(const QString &s) {
    QString o = s;
    o.replace("#", "#22");
    o.replace(":", "#3A");
    return o;
}

QString GameInfo::obsWindowString() const {
    return GameDetector::escapeObs(title) + ":" + GameDetector::escapeObs(windowClass) + ":" + GameDetector::escapeObs(exe);
}

bool GameDetector::isExcludedByDefault(const QString &e) {
    static const QSet<QString> skip = {
        "chrome.exe", "msedge.exe", "firefox.exe", "brave.exe", "opera.exe", "opera_gx.exe", "vivaldi.exe", "arc.exe", "iexplore.exe",
        "discord.exe", "discordptb.exe", "discordcanary.exe", "spotify.exe", "vlc.exe", "mpv.exe", "mpc-hc64.exe", "mpc-hc.exe",
        "wmplayer.exe", "itunes.exe", "netflix.exe", "twitch.exe", "obs64.exe", "obs32.exe", "streamlabs obs.exe", "prismlivestudio.exe",
        "steam.exe", "steamwebhelper.exe", "epicgameslauncher.exe", "battle.net.exe", "galaxyclient.exe", "upc.exe", "ubisoftconnect.exe",
        "eadesktop.exe", "origin.exe", "riotclientux.exe", "riotclientservices.exe", "leagueclient.exe", "leagueclientux.exe",
        "teams.exe", "ms-teams.exe", "zoom.exe", "slack.exe", "telegram.exe", "whatsapp.exe", "signal.exe", "skype.exe",
        "explorer.exe", "applicationframehost.exe", "searchhost.exe", "shellexperiencehost.exe", "startmenuexperiencehost.exe",
        "textinputhost.exe", "systemsettings.exe", "lockapp.exe", "dwm.exe", "taskmgr.exe", "code.exe", "devenv.exe",
        "windowsterminal.exe", "cmd.exe", "powershell.exe", "pwsh.exe", "notepad.exe", "medal.exe", "medalencoder.exe",
        "nvidia share.exe", "nvidia app.exe", "gamebar.exe", "klipit.exe", "photos.exe", "video.ui.exe", "winword.exe",
        "excel.exe", "powerpnt.exe", "outlook.exe", "blender.exe", "unity.exe", "unrealeditor.exe", "photoshop.exe",
        "premiere pro.exe", "afterfx.exe", "resolve.exe", "vegas.exe", "acrobat.exe", "acrord32.exe"};
    return skip.contains(e);
}

QString GameDetector::friendlyName(const QString &exe, const QString &product) {
    QString p = product.trimmed();
    static const QStringList generic = {"", "Unreal Engine", "UnrealGame", "Unity", "Unity Player", "BootstrapPackagedGame", "Game"};
    if (!generic.contains(p, Qt::CaseInsensitive)) return p;
    QString b = QFileInfo(exe).completeBaseName();
    for (const char *suffix : {"-Win64-Shipping", "-Win32-Shipping", "_x64", "64", "_dx12", "_dx11", "-Shipping"})
        if (b.endsWith(QLatin1String(suffix), Qt::CaseInsensitive)) b.chop(int(strlen(suffix)));
    return b.isEmpty() ? exe : b;
}

void GameDetector::start() {
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &GameDetector::poll, Qt::UniqueConnection);
    m_timer.start();
    poll();
}

void GameDetector::stop() { m_timer.stop(); }

void GameDetector::setManualGame(const GameInfo &g) {
    if (m_current.valid()) Q_EMIT gameEnded(m_current);
    m_manual = true;
    m_current = g;
    Q_EMIT gameStarted(g);
}

void GameDetector::clearManualGame() {
    if (!m_manual) return;
    m_manual = false;
    const GameInfo old = m_current;
    m_current = {};
    if (old.valid()) Q_EMIT gameEnded(old);
}

#ifdef _WIN32
static QString processPath(HANDLE h) {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = DWORD(std::size(buf));
    if (!QueryFullProcessImageNameW(h, 0, buf, &n)) return {};
    return QString::fromWCharArray(buf, int(n));
}

static bool uses3D(HANDLE h) {
    HMODULE mods[1024];
    DWORD need = 0;
    if (!EnumProcessModulesEx(h, mods, sizeof(mods), &need, LIST_MODULES_ALL)) return false;
    const int n = int(qMin<DWORD>(need / sizeof(HMODULE), DWORD(std::size(mods))));
    for (int i = 0; i < n; ++i) {
        wchar_t name[MAX_PATH];
        if (!GetModuleBaseNameW(h, mods[i], name, MAX_PATH)) continue;
        const QString m = QString::fromWCharArray(name).toLower();
        if (m == "d3d11.dll" || m == "d3d12.dll" || m == "d3d9.dll" || m == "d3d10.dll" || m == "d3d10_1.dll" ||
            m == "vulkan-1.dll" || m == "opengl32.dll")
            return true;
    }
    return false;
}

static QString productName(const QString &path) {
    const std::wstring p = path.toStdWString();
    DWORD dummy = 0;
    const DWORD size = GetFileVersionInfoSizeW(p.c_str(), &dummy);
    if (!size) return {};
    QByteArray data(int(size), 0);
    if (!GetFileVersionInfoW(p.c_str(), 0, size, data.data())) return {};
    struct Lang { WORD lang, cp; } *langs = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void **>(&langs), &len) || !len) return {};
    for (const wchar_t *key : {L"ProductName", L"FileDescription"}) {
        wchar_t sub[128];
        swprintf(sub, 128, L"\\StringFileInfo\\%04x%04x\\%s", langs[0].lang, langs[0].cp, key);
        wchar_t *val = nullptr;
        UINT vl = 0;
        if (VerQueryValueW(data.data(), sub, reinterpret_cast<void **>(&val), &vl) && vl > 1) return QString::fromWCharArray(val).trimmed();
    }
    return {};
}

static bool coversMonitor(HWND w, QRect *out) {
    RECT r;
    if (!GetWindowRect(w, &r)) return false;
    *out = QRect(QPoint(r.left, r.top), QPoint(r.right - 1, r.bottom - 1));
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}

void GameDetector::poll() {
    if (m_manual) return;
    // 1) did the current game exit?
    if (m_current.valid()) {
        DWORD code = 0;
        if (!m_proc || !GetExitCodeProcess(HANDLE(m_proc), &code) || code != STILL_ACTIVE) {
            if (m_proc) CloseHandle(HANDLE(m_proc));
            m_proc = nullptr;
            const GameInfo old = m_current;
            m_current = {};
            Q_EMIT gameEnded(old);
        }
    }
    // 2) is the window in front a game?
    HWND fg = GetForegroundWindow();
    if (!fg || !IsWindowVisible(fg) || IsIconic(fg) || GetWindow(fg, GW_OWNER)) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid || pid == GetCurrentProcessId() || pid == m_current.pid) {
        if (pid == m_current.pid && m_current.valid()) {   // keep the window handle fresh (games recreate windows)
            m_current.hwnd = quintptr(fg);
            QRect r; coversMonitor(fg, &r); m_current.windowRect = r;
        }
        return;
    }
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!h) return;
    const QString path = processPath(h);
    const QString exe = QFileInfo(path).fileName();
    const QString low = exe.toLower();
    auto &s = Settings::instance();
    QStringList include, exclude;
    for (const auto &x : s.list("games.include")) include << x.toLower();
    for (const auto &x : s.list("games.exclude")) exclude << x.toLower();

    bool game = false;
    QRect rect;
    const bool full = coversMonitor(fg, &rect);
    if (include.contains(low)) game = true;
    else if (!exclude.contains(low) && !isExcludedByDefault(low) && !path.contains("\\Windows\\", Qt::CaseInsensitive)) {
        const QString lp = path.toLower();
        const bool inLibrary = lp.contains("\\steamapps\\common\\") || lp.contains("\\epic games\\") || lp.contains("\\riot games\\") ||
                               lp.contains("\\gog games\\") || lp.contains("\\xboxgames\\") || lp.contains("\\windowsapps\\") ||
                               lp.contains("\\ubisoft game launcher\\games\\") || lp.contains("\\ea games\\") || lp.contains("\\roblox\\") ||
                               lp.contains("\\battle.net\\") || lp.contains("\\games\\");
        if ((full || inLibrary) && uses3D(h)) game = true;
        if (lp.contains("\\windowsapps\\") && !full) game = false;   // store apps: only when fullscreen
    }
    if (!game) { CloseHandle(h); return; }

    GameInfo g;
    g.pid = pid; g.exe = exe; g.path = path; g.hwnd = quintptr(fg); g.windowRect = rect;
    g.name = friendlyName(exe, productName(path));
    using Fn = int(WINAPI *)(HWND, LPWSTR, int);
    static Fn internalGetText = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "InternalGetWindowText"));
    wchar_t buf[512] = {};
    if (internalGetText) internalGetText(fg, buf, 512);   // never sends window messages, so a hung game can't freeze us
    g.title = QString::fromWCharArray(buf);
    wchar_t cls[256] = {};
    GetClassNameW(fg, cls, 256);
    g.windowClass = QString::fromWCharArray(cls);

    if (m_current.valid()) {   // another game took over
        if (m_proc) CloseHandle(HANDLE(m_proc));
        const GameInfo old = m_current;
        m_current = {};
        Q_EMIT gameEnded(old);
    }
    m_proc = h;
    m_current = g;
    qInfo("Game detected: %s (%s)", qPrintable(g.name), qPrintable(g.exe));
    Q_EMIT gameStarted(g);
}
#else
void GameDetector::poll() {}
#endif

} // namespace kl
