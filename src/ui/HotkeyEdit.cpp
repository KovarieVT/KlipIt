#include "ui/HotkeyEdit.h"
#include "core/HotkeyManager.h"
#include <QKeyEvent>
#include <QStyle>

namespace kl {

HotkeyEdit::HotkeyEdit(const QString &id, QLabel *msg, QWidget *parent) : QKeySequenceEdit(parent), m_id(id), m_msg(msg) {
    setKeySequence(HotkeyManager::instance().binding(id));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    setMaximumSequenceLength(1);
    setClearButtonEnabled(true);
#endif
    setToolTip(tr("Click, then press the key you want (Ctrl, Alt and Shift combinations work). Esc cancels."));
    connect(this, &QKeySequenceEdit::editingFinished, this, &HotkeyEdit::commit);
    connect(this, &QKeySequenceEdit::keySequenceChanged, this, [this](const QKeySequence &k) {
        if (k.isEmpty() && hasFocus()) commit();   // cleared with the clear button
    });
}

void HotkeyEdit::focusInEvent(QFocusEvent *e) {
    HotkeyManager::instance().suspend(true);
    if (m_msg) { m_msg->setText(tr("Press the new key now...")); m_msg->setObjectName("Hint"); }
    QKeySequenceEdit::focusInEvent(e);
}

void HotkeyEdit::focusOutEvent(QFocusEvent *e) {
    QKeySequenceEdit::focusOutEvent(e);
    HotkeyManager::instance().suspend(false);
    if (m_msg && m_msg->text() == tr("Press the new key now...")) m_msg->clear();
}

void HotkeyEdit::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        setKeySequence(HotkeyManager::instance().binding(m_id));
        clearFocus();
        return;
    }
    QKeySequenceEdit::keyPressEvent(e);
    // single key combinations: finish as soon as a real key (not just a modifier) is pressed
    if (!keySequence().isEmpty()) { clearFocus(); }
}

void HotkeyEdit::commit() {
    QKeySequence seq = keySequence();
    if (seq.count() > 1) seq = QKeySequence(seq[0]);
    auto &hm = HotkeyManager::instance();
    const QString err = hm.setBinding(m_id, seq);
    if (!err.isEmpty()) {
        setKeySequence(hm.binding(m_id));
        if (m_msg) { m_msg->setObjectName("Warn"); m_msg->setText(err); }
    } else if (m_msg) {
        m_msg->setObjectName("Hint");
        m_msg->setText(seq.isEmpty() ? tr("Hotkey removed.") : tr("Saved: %1").arg(seq.toString(QKeySequence::NativeText)));
    }
    if (m_msg) { m_msg->style()->unpolish(m_msg); m_msg->style()->polish(m_msg); }
}

} // namespace kl
