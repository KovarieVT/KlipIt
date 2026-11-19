#pragma once
#include "ui/ClipGrid.h"
#include <QWidget>

class QLabel;
class QComboBox;
class QLineEdit;
class QVBoxLayout;
class QListWidget;
class QSpinBox;
class QCheckBox;
class QPushButton;

namespace kl {

// Common page frame: gothic title, subtitle and a scrollable body.
class Page : public QWidget {
    Q_OBJECT
public:
    Page(const QString &title, const QString &subtitle, bool scroll = true, QWidget *parent = nullptr);
    QVBoxLayout *body() const { return m_body; }
    static QWidget *card(QVBoxLayout **inner, const QString &title = {}, const QString &hint = {});
    static QLabel *hint(const QString &text);
Q_SIGNALS:
    void openClip(const QString &path);
    void navigate(const QString &page);
protected:
    QVBoxLayout *m_body;
};

class HomePage : public Page {
    Q_OBJECT
public:
    explicit HomePage(QWidget *parent = nullptr);
    void refresh();
private:
    QLabel *m_state, *m_detail, *m_hotkeys, *m_clips, *m_sessions, *m_shots, *m_storage;
};

// Library-style grid with search, game filter and type chips. Library, Clips,
// Screenshots, Favorites and Highlights are all this page with a different filter.
class LibraryPage : public Page {
    Q_OBJECT
public:
    LibraryPage(const QString &title, const QString &sub, const ClipFilter &base, bool kindChips, QWidget *parent = nullptr);
private:
    void apply();
    ClipFilter m_base;
    ClipGrid *m_grid;
    QLineEdit *m_search;
    QComboBox *m_game;
    QString m_kind;
    QLabel *m_count;
};

class HighlightsPage : public LibraryPage {
    Q_OBJECT
public:
    explicit HighlightsPage(QWidget *parent = nullptr);
};

class GamesPage : public Page {
    Q_OBJECT
public:
    explicit GamesPage(QWidget *parent = nullptr);
    void refresh();
private:
    void showProfile(const QString &key);
    QListWidget *m_list;
    QWidget *m_editor;
    QString m_key;
};

class CapturePage : public Page {
    Q_OBJECT
public:
    explicit CapturePage(QWidget *parent = nullptr);
    void refresh();
private:
    QLabel *m_estimate;
};

class ReplayPage : public Page {
    Q_OBJECT
public:
    explicit ReplayPage(QWidget *parent = nullptr);
    void refresh();
private:
    void setLength(int seconds);
    QSpinBox *m_custom, *m_post;
    QLabel *m_total, *m_state, *m_ram;
    QList<QPushButton *> m_presets;
    QPushButton *m_toggle;
};

class SettingsPage : public Page {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);
};

class PluginsPage : public Page {
    Q_OBJECT
public:
    explicit PluginsPage(QWidget *parent = nullptr);
    void refresh();
private:
    QListWidget *m_list;
};

} // namespace kl
