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

// System-wide hotkeys (they work while a game has focus) via RegisterHotKey.
// While the user is choosing a new key, suspend() releases every hotkey so
// Windows hands the key to KlipIt's settings box instead of firing the action.
class HotkeyManager : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    static HotkeyManager &instance();
    void setHostWindow(QWidget *w) { m_host = w; }

    void registerAction(const QString &id, const QString &label, std::function<void()> fn);
    QList<HotkeyAction> actions() const { return m_actions; }

    QKeySequence binding(const QString &id) const;
    QString conflictFor(const QString &id, const QKeySequence &seq) const;   // empty = OK
    QString setBinding(const QString &id, const QKeySequence &seq);          // empty = saved
    void applyAll();
    void suspend(bool on);
    bool isSuspended() const { return m_suspended; }
    QStringList failed() const { return m_failed; }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
    static unsigned toVirtualKey(int qtKey);   // 0 = not usable as a global hotkey

Q_SIGNALS:
    void triggered(const QString &id);

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
