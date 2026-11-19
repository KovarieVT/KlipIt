#pragma once
#include <QHash>
#include <QMainWindow>
#include <QSystemTrayIcon>

class QStackedWidget;
class QLabel;
class QPushButton;
class QButtonGroup;
class QSoundEffect;

namespace kl {

class EditorPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void showPage(const QString &id);
    void openClip(const QString &path);
    QSystemTrayIcon *tray() const { return m_tray; }

protected:
    void closeEvent(QCloseEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    void buildTray();
    void refreshStatus();
    void onClipSaved(const QString &path, const QString &game, bool highlight);
    QStackedWidget *m_stack;
    QHash<QString, QWidget *> m_pages;
    QHash<QString, QPushButton *> m_nav;
    EditorPage *m_editor = nullptr;
    QLabel *m_statusText, *m_statusSub, *m_dot;
    QPushButton *m_saveBtn, *m_recBtn, *m_shotBtn, *m_pauseBtn;
    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_trayRec = nullptr, *m_trayPause = nullptr;
    QSoundEffect *m_sound = nullptr;
    bool m_quitting = false, m_trayHintShown = false;
};

} // namespace kl
