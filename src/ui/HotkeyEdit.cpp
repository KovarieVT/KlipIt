#include "ui/HotkeyEdit.h"
#include "core/Theme.h"
#include <QAction>
#include <QKeyEvent>
#include <QPainter>
#include <QStyle>

namespace kl {

static QIcon clearIcon() {
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(theme::current().muted, 1.6, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(4.5, 4.5), QPointF(11.5, 11.5));
    p.drawLine(QPointF(11.5, 4.5), QPointF(4.5, 11.5));
    return QIcon(pm);
}

HotkeyEdit::HotkeyEdit(const QString &id, QLabel *msg, QWidget *parent) : QLineEdit(parent), m_id(id), m_msg(msg) {
    setReadOnly(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumWidth(190);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Click, then press any key, including numpad keys like Num *. Ctrl, Alt, Shift and Win combinations work too.\n"
                  "Esc cancels. The × removes the hotkey."));
    QAction *clear = addAction(clearIcon(), QLineEdit::TrailingPosition);
    clear->setToolTip(tr("Remove this hotkey"));
    connect(clear, &QAction::triggered, this, [this] {
        const QString err = HotkeyManager::instance().setBinding(m_id, Hotkey{});
        m_waiting = false;
        clearFocus();
        showCurrent();
        say(err.isEmpty() ? tr("Hotkey removed.") : err, !err.isEmpty());
    });
    connect(&HotkeyManager::instance(), &HotkeyManager::bindingsChanged, this, [this] { if (!m_waiting) showCurrent(); });
    showCurrent();
}

void HotkeyEdit::showCurrent() {
    const Hotkey k = HotkeyManager::instance().binding(m_id);
    setText(k.isEmpty() ? QString() : k.text);
    setPlaceholderText(tr("No hotkey"));
}

void HotkeyEdit::say(const QString &text, bool warn) {
    if (!m_msg) return;
    m_msg->setText(text);
    m_msg->setObjectName(warn ? "Warn" : "Hint");
    m_msg->style()->unpolish(m_msg);
    m_msg->style()->polish(m_msg);
}

quint32 HotkeyEdit::modsOf(Qt::KeyboardModifiers m) {
    quint32 r = 0;
    if (m & Qt::AltModifier) r |= 1;
    if (m & Qt::ControlModifier) r |= 2;
    if (m & Qt::ShiftModifier) r |= 4;
    if (m & Qt::MetaModifier) r |= 8;
    return r;
}

void HotkeyEdit::focusInEvent(QFocusEvent *e) {
    QLineEdit::focusInEvent(e);
    m_waiting = true;
    HotkeyManager::instance().suspend(true);   // so the key reaches this box instead of firing an action
    setText(QString());
    setPlaceholderText(tr("Press any key..."));
    say(tr("Press the key you want for this action..."), false);
}

void HotkeyEdit::focusOutEvent(QFocusEvent *e) {
    QLineEdit::focusOutEvent(e);
    if (m_waiting) {   // left without choosing: keep the old key
        m_waiting = false;
        if (m_msg && m_msg->text() == tr("Press the key you want for this action...")) m_msg->clear();
    }
    HotkeyManager::instance().suspend(false);
    showCurrent();
}

void HotkeyEdit::mousePressEvent(QMouseEvent *e) {
    QLineEdit::mousePressEvent(e);
    if (!hasFocus()) setFocus(Qt::MouseFocusReason);
}

bool HotkeyEdit::event(QEvent *e) {
    // Tab / Shift+Tab would normally move focus away; while waiting they're keys like any other
    if (m_waiting && e->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(e);
        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) { keyPressEvent(ke); return true; }
    }
    if (m_waiting && e->type() == QEvent::ShortcutOverride) { e->accept(); return true; }   // no shortcuts while choosing
    return QLineEdit::event(e);
}

void HotkeyEdit::keyPressEvent(QKeyEvent *e) {
    if (!m_waiting) { QLineEdit::keyPressEvent(e); return; }
    const int key = e->key();
    const quint32 mods = modsOf(e->modifiers());
    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_AltGr ||
        key == Qt::Key_Super_L || key == Qt::Key_Super_R) {
        setPlaceholderText(Hotkey::modsText(mods) + "...");   // show the held modifiers while waiting for the key
        return;
    }
    if (key == Qt::Key_Escape && mods == 0) {   // Esc alone cancels (Shift+Esc etc. can still be chosen)
        m_waiting = false;
        say(tr("Cancelled, the hotkey wasn't changed."), false);
        clearFocus();
        return;
    }
    quint32 vk = 0;
#ifdef _WIN32
    vk = e->nativeVirtualKey();   // the exact key, e.g. 0x6A for Num *
#endif
    if (vk == 0 || vk == 0xE5 /* IME processing */) vk = HotkeyManager::toVirtualKey(key, e->modifiers() & Qt::KeypadModifier);
    if (vk == 0x2C) return;       // Print Screen arrives on key release
    if (vk == 0) { say(tr("KlipIt couldn't read that key. Try another one."), true); return; }
    accept(vk, mods);
}

void HotkeyEdit::keyReleaseEvent(QKeyEvent *e) {
    // Windows only sends Print Screen when it's released
    if (m_waiting && (e->key() == Qt::Key_Print
#ifdef _WIN32
                      || e->nativeVirtualKey() == 0x2C
#endif
                      )) {
        accept(0x2C, modsOf(e->modifiers()));
        return;
    }
    QLineEdit::keyReleaseEvent(e);
}

void HotkeyEdit::accept(quint32 vk, quint32 mods) {
    const Hotkey k = Hotkey::fromKey(vk, mods);
    m_waiting = false;
    clearFocus();   // gives the hotkeys back, including the new one
    const QString err = HotkeyManager::instance().setBinding(m_id, k);
    showCurrent();
    say(err.isEmpty() ? tr("Saved: %1").arg(k.text) : err, !err.isEmpty());
}

} // namespace kl
