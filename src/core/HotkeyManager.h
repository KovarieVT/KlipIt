#pragma once
#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <functional>

class QShortcut;
class QWidget;

namespace kl {

struct HotkeyAction { QString id, label; std::function<void()> fn; };

// One hotkey: a Windows virtual-key code plus modifiers, so ANY key works,
// including every numpad key (Num *, Num 5, Num Enter...), F13-F24, media keys,
// Print Screen and keys that only exist on some keyboard layouts.
struct Hotkey {
    quint32 vk = 0;          // Windows virtual-key code (0 = no hotkey)
    quint32 mods = 0;        // 1 Alt, 2 Ctrl, 4 Shift, 8 Win (the same bits RegisterHotKey uses)
    QString text;            // what people see, e.g. "Ctrl+Num *"
    bool isEmpty() const { return vk == 0; }
    bool sameKeys(const Hotkey &o) const { return vk == o.vk && mods == o.mods; }
    QString stored() const;                     // "Ctrl+Num *|106|2"
    static Hotkey parse(const QString &stored); // also reads the old "F8" / "Ctrl+F10" style
    static Hotkey fromKey(quint32 vk, quint32 mods);
    static QString keyName(quint32 vk);         // "Num *", "F8", "Page Up"...
    static QString modsText(quint32 mods);      // "Ctrl+Shift+"
};

// System-wide hotkeys (they work while a game has focus) via RegisterHotKey.
// While you're choosing a new key, suspend() releases every hotkey so Windows
// hands the key to KlipIt's settings box instead of firing the action.
class HotkeyManager : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    static HotkeyManager &instance();
    void setHostWindow(QWidget *w) { m_host = w; }

    void registerAction(const QString &id, const QString &label, std::function<void()> fn);
    QList<HotkeyAction> actions() const { return m_actions; }

    Hotkey binding(const QString &id) const;
    QString displayText(const QString &id) const;                 // "Num *", or "no key"
    QString conflictFor(const QString &id, const Hotkey &k) const; // empty = OK
    QString setBinding(const QString &id, const Hotkey &k);        // empty = saved
    void applyAll();
    void suspend(bool on);
    bool isSuspended() const { return m_suspended; }
    QStringList failed() const { return m_failed; }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
    // Qt key (+ keypad flag) -> Windows virtual-key code, used for the old setting format and non-Windows builds.
    static quint32 toVirtualKey(int qtKey, bool keypad = false);

Q_SIGNALS:
    void triggered(const QString &id);
    void bindingsChanged();

private:
    HotkeyManager() = default;
    void unregisterAll();
    QList<HotkeyAction> m_actions;
    QHash<int, QString> m_native;
    QHash<QString, QShortcut *> m_shortcuts;
    QStringList m_failed;
    QWidget *m_host = nullptr;
    int m_nextId = 0x4B4C;
    bool m_suspended = false;
};

} // namespace kl
