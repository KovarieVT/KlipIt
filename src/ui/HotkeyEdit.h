#pragma once
#include <QKeySequenceEdit>
#include <QLabel>

namespace kl {

// A hotkey box that actually works: while it has focus, KlipIt lets go of its
// global hotkeys so the key you press lands here instead of saving a clip.
class HotkeyEdit : public QKeySequenceEdit {
    Q_OBJECT
public:
    HotkeyEdit(const QString &actionId, QLabel *message, QWidget *parent = nullptr);

protected:
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void commit();
    QString m_id;
    QLabel *m_msg;
};

} // namespace kl
