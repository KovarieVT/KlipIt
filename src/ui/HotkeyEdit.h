#pragma once
#include "core/HotkeyManager.h"
#include <QLabel>
#include <QLineEdit>

namespace kl {

// Click it, press any key (with Ctrl / Alt / Shift / Win if you like) and that's
// the new hotkey. Every key works, including all numpad keys like Num *.
// While it's waiting for your key, KlipIt lets go of its global hotkeys so the
// key lands here instead of saving a clip. Esc alone cancels; × clears.
class HotkeyEdit : public QLineEdit {
    Q_OBJECT
public:
    HotkeyEdit(const QString &actionId, QLabel *message, QWidget *parent = nullptr);

protected:
    bool event(QEvent *e) override;          // also catches Tab, which Qt normally uses to move focus
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    void showCurrent();
    void accept(quint32 vk, quint32 mods);
    void say(const QString &text, bool warn);
    static quint32 modsOf(Qt::KeyboardModifiers m);
    QString m_id;
    QLabel *m_msg;
    bool m_waiting = false;
};

} // namespace kl
