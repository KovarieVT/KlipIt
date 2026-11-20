#include "core/HotkeyManager.h"
#include "core/Settings.h"
#include <QShortcut>
#include <QWidget>

#ifdef _WIN32
#include <windows.h>
#endif

namespace kl {

// ------------------------------------------------------------------ Hotkey
QString Hotkey::modsText(quint32 m) {
    QString s;
    if (m & 2) s += "Ctrl+";
    if (m & 1) s += "Alt+";
    if (m & 4) s += "Shift+";
    if (m & 8) s += "Win+";
    return s;
}

QString Hotkey::keyName(quint32 vk) {
    if (vk >= 0x70 && vk <= 0x87) return QStringLiteral("F%1").arg(vk - 0x70 + 1);
    if (vk >= 0x60 && vk <= 0x69) return QStringLiteral("Num %1").arg(vk - 0x60);
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return QString(QChar(char(vk)));
    switch (vk) {
    case 0x6A: return "Num *";       case 0x6B: return "Num +";      case 0x6C: return "Num Separator";
    case 0x6D: return "Num -";       case 0x6E: return "Num .";      case 0x6F: return "Num /";
    case 0x08: return "Backspace";   case 0x09: return "Tab";        case 0x0C: return "Clear";
    case 0x0D: return "Enter";       case 0x13: return "Pause";      case 0x14: return "Caps Lock";
    case 0x1B: return "Esc";         case 0x20: return "Space";      case 0x21: return "Page Up";
    case 0x22: return "Page Down";   case 0x23: return "End";        case 0x24: return "Home";
    case 0x25: return "Left";        case 0x26: return "Up";         case 0x27: return "Right";
    case 0x28: return "Down";        case 0x2C: return "Print Screen"; case 0x2D: return "Insert";
    case 0x2E: return "Delete";      case 0x5D: return "Menu";       case 0x90: return "Num Lock";
    case 0x91: return "Scroll Lock"; case 0x5B: return "Left Win";   case 0x5C: return "Right Win";
    case 0xA6: return "Browser Back"; case 0xA7: return "Browser Forward"; case 0xA8: return "Browser Refresh";
    case 0xA9: return "Browser Stop"; case 0xAA: return "Browser Search"; case 0xAB: return "Browser Favorites";
    case 0xAC: return "Browser Home"; case 0xAD: return "Mute";      case 0xAE: return "Volume Down";
    case 0xAF: return "Volume Up";   case 0xB0: return "Next Track"; case 0xB1: return "Previous Track";
    case 0xB2: return "Stop Media";  case 0xB3: return "Play/Pause"; case 0xB4: return "Mail";
    case 0xB5: return "Media Select"; case 0xB6: return "Launch App 1"; case 0xB7: return "Launch App 2";
    default: break;
    }
#ifdef _WIN32
    // anything else (punctuation and layout-specific keys): ask Windows for the key's own name
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (sc) {
        wchar_t buf[64] = {};
        if (GetKeyNameTextW(LONG(sc << 16), buf, 64) > 0) return QString::fromWCharArray(buf);
    }
#endif
    switch (vk) {
    case 0xBA: return ";"; case 0xBB: return "="; case 0xBC: return ","; case 0xBD: return "-"; case 0xBE: return ".";
    case 0xBF: return "/"; case 0xC0: return "`"; case 0xDB: return "["; case 0xDC: return "\\"; case 0xDD: return "]";
    case 0xDE: return "'"; case 0xE2: return "<";
    default: return QStringLiteral("Key %1").arg(vk);
    }
}

Hotkey Hotkey::fromKey(quint32 vk, quint32 mods) {
    Hotkey k;
    k.vk = vk;
    k.mods = mods & 0xF;
    if (vk) k.text = modsText(k.mods) + keyName(vk);
    return k;
}

QString Hotkey::stored() const {
    return isEmpty() ? QString() : QStringLiteral("%1|%2|%3").arg(text).arg(vk).arg(mods);
}

Hotkey Hotkey::parse(const QString &s) {
    if (s.trimmed().isEmpty()) return {};
    const QStringList p = s.split('|');
    if (p.size() == 3) {   // current format: text|vk|mods
        Hotkey k = fromKey(p[1].toUInt(), p[2].toUInt());
        if (!p[0].isEmpty()) k.text = p[0];
        return k;
    }
    // older format, e.g. "F8", "Ctrl+F10" or "Num+*"
    const QKeySequence seq(s, QKeySequence::PortableText);
    if (seq.isEmpty()) return {};
    const QKeyCombination kc = seq[0];
    quint32 mods = 0;
    if (kc.keyboardModifiers() & Qt::AltModifier) mods |= 1;
    if (kc.keyboardModifiers() & Qt::ControlModifier) mods |= 2;
    if (kc.keyboardModifiers() & Qt::ShiftModifier) mods |= 4;
    if (kc.keyboardModifiers() & Qt::MetaModifier) mods |= 8;
    return fromKey(HotkeyManager::toVirtualKey(kc.key(), kc.keyboardModifiers() & Qt::KeypadModifier), mods);
}

// ------------------------------------------------------------------ manager
HotkeyManager &HotkeyManager::instance() {
    static HotkeyManager h;
    return h;
}

void HotkeyManager::registerAction(const QString &id, const QString &label, std::function<void()> fn) {
    for (auto &a : m_actions) if (a.id == id) { a.fn = std::move(fn); a.label = label; return; }
    m_actions << HotkeyAction{id, label, std::move(fn)};
}

Hotkey HotkeyManager::binding(const QString &id) const { return Hotkey::parse(Settings::instance().s("hotkeys." + id)); }

QString HotkeyManager::displayText(const QString &id) const {
    const Hotkey k = binding(id);
    return k.isEmpty() ? tr("no key") : k.text;
}

QString HotkeyManager::conflictFor(const QString &id, const Hotkey &k) const {
    if (k.isEmpty()) return {};
    for (const auto &a : m_actions)
        if (a.id != id && binding(a.id).sameKeys(k)) return tr("Already used by \"%1\".").arg(a.label);
    return {};
}

QString HotkeyManager::setBinding(const QString &id, const Hotkey &k) {
    const QString c = conflictFor(id, k);
    if (!c.isEmpty()) return c;
    Settings::instance().set("hotkeys." + id, k.stored());
    Settings::instance().save();
    if (!m_suspended) applyAll();
    Q_EMIT bindingsChanged();
    if (m_failed.contains(id)) return tr("Windows or another program already uses %1. Pick a different key.").arg(k.text);
    return {};
}

quint32 HotkeyManager::toVirtualKey(int k, bool keypad) {
    if (keypad) {
        if (k >= Qt::Key_0 && k <= Qt::Key_9) return 0x60u + quint32(k - Qt::Key_0);
        switch (k) {
        case Qt::Key_Asterisk: return 0x6A; case Qt::Key_Plus: return 0x6B; case Qt::Key_Minus: return 0x6D;
        case Qt::Key_Period: return 0x6E; case Qt::Key_Comma: return 0x6E; case Qt::Key_Slash: return 0x6F;
        case Qt::Key_Enter: case Qt::Key_Return: return 0x0D;
        default: break;
        }
    }
    if (k >= Qt::Key_A && k <= Qt::Key_Z) return quint32('A' + (k - Qt::Key_A));
    if (k >= Qt::Key_0 && k <= Qt::Key_9) return quint32('0' + (k - Qt::Key_0));
    if (k >= Qt::Key_F1 && k <= Qt::Key_F24) return 0x70u + quint32(k - Qt::Key_F1);
    switch (k) {
    case Qt::Key_Backspace: return 0x08; case Qt::Key_Tab: case Qt::Key_Backtab: return 0x09; case Qt::Key_Return: case Qt::Key_Enter: return 0x0D;
    case Qt::Key_Pause: return 0x13; case Qt::Key_CapsLock: return 0x14; case Qt::Key_Escape: return 0x1B;
    case Qt::Key_Space: return 0x20; case Qt::Key_PageUp: return 0x21; case Qt::Key_PageDown: return 0x22;
    case Qt::Key_End: return 0x23; case Qt::Key_Home: return 0x24; case Qt::Key_Left: return 0x25;
    case Qt::Key_Up: return 0x26; case Qt::Key_Right: return 0x27; case Qt::Key_Down: return 0x28;
    case Qt::Key_Print: return 0x2C; case Qt::Key_Insert: return 0x2D; case Qt::Key_Delete: return 0x2E;
    case Qt::Key_Menu: return 0x5D; case Qt::Key_NumLock: return 0x90; case Qt::Key_ScrollLock: return 0x91;
    case Qt::Key_Asterisk: return 0x6A; case Qt::Key_Plus: return 0xBB;
    case Qt::Key_Semicolon: return 0xBA; case Qt::Key_Equal: return 0xBB; case Qt::Key_Comma: return 0xBC;
    case Qt::Key_Minus: return 0xBD; case Qt::Key_Period: return 0xBE; case Qt::Key_Slash: return 0xBF;
    case Qt::Key_QuoteLeft: return 0xC0; case Qt::Key_BracketLeft: return 0xDB; case Qt::Key_Backslash: return 0xDC;
    case Qt::Key_BracketRight: return 0xDD; case Qt::Key_Apostrophe: return 0xDE;
    case Qt::Key_VolumeMute: return 0xAD; case Qt::Key_VolumeDown: return 0xAE; case Qt::Key_VolumeUp: return 0xAF;
    case Qt::Key_MediaNext: return 0xB0; case Qt::Key_MediaPrevious: return 0xB1; case Qt::Key_MediaStop: return 0xB2;
    case Qt::Key_MediaPlay: case Qt::Key_MediaTogglePlayPause: return 0xB3;
    default: return 0;
    }
}

void HotkeyManager::unregisterAll() {
#ifdef _WIN32
    for (int id : m_native.keys()) UnregisterHotKey(nullptr, id);
#endif
    m_native.clear();
    qDeleteAll(m_shortcuts);
    m_shortcuts.clear();
}

void HotkeyManager::suspend(bool on) {
    if (on == m_suspended) return;
    m_suspended = on;
    if (on) unregisterAll();
    else applyAll();
}

void HotkeyManager::applyAll() {
    unregisterAll();
    m_failed.clear();
    if (m_suspended) return;
    for (const auto &a : m_actions) {
        const Hotkey k = binding(a.id);
        if (k.isEmpty()) continue;
#ifdef _WIN32
        UINT mods = MOD_NOREPEAT;
        if (k.mods & 1) mods |= MOD_ALT;
        if (k.mods & 2) mods |= MOD_CONTROL;
        if (k.mods & 4) mods |= MOD_SHIFT;
        if (k.mods & 8) mods |= MOD_WIN;
        const int id = m_nextId++;
        if (RegisterHotKey(nullptr, id, mods, k.vk)) m_native.insert(id, a.id);
        else { m_failed << a.id; qWarning("Hotkey %s for %s is taken by another program", qPrintable(k.text), qPrintable(a.id)); }
#else
        // other systems (tests): app-wide shortcut from the older text form when possible
        if (!m_host) continue;
        const QKeySequence seq(Settings::instance().s("hotkeys." + a.id).section('|', 0, 0), QKeySequence::PortableText);
        if (seq.isEmpty()) continue;
        auto *sc = new QShortcut(seq, m_host);
        sc->setContext(Qt::ApplicationShortcut);
        const QString id = a.id;
        connect(sc, &QShortcut::activated, this, [this, id] {
            for (const auto &x : m_actions) if (x.id == id && x.fn) x.fn();
            Q_EMIT triggered(id);
        });
        m_shortcuts.insert(a.id, sc);
#endif
    }
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) {
#ifdef _WIN32
    if (eventType != "windows_generic_MSG") return false;
    const MSG *msg = static_cast<MSG *>(message);
    if (msg->message != WM_HOTKEY) return false;
    const QString id = m_native.value(int(msg->wParam));
    if (id.isEmpty()) return false;
    for (const auto &a : m_actions) if (a.id == id && a.fn) a.fn();
    Q_EMIT triggered(id);
    return true;
#else
    Q_UNUSED(eventType); Q_UNUSED(message);
    return false;
#endif
}

} // namespace kl
