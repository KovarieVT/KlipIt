#include "core/Settings.h"
#include "core/Paths.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace kl {

Settings &Settings::instance() {
    static Settings s;
    return s;
}

QJsonObject Settings::defaults() {
    return QJsonObject{
        {"version", 1},
        {"general", QJsonObject{
            {"startWithWindows", false},
            {"closeToTray", true},
            {"clipSound", true},          // the clip-saved chime, played at 50 % volume
            {"notifications", true},      // the "Clip saved" pop-up
            {"animations", true},         // splash screen and decorative animations
        }},
        {"appearance", QJsonObject{{"theme", "black"}}},   // black | darkgrey | white | blue | purple
        {"capture", QJsonObject{
            {"mode", "game"},             // game = detect games and hook them, screen = record a whole monitor
            {"onlyWhenGame", true},       // game mode: keep the buffer off while no game is open
            {"display", -1},              // -1 = automatic (the monitor the game is on), 0.. = a specific monitor
            {"resolution", "1440"},       // native | 1440 | 1080 | 720 (never upscales)
            {"fps", 60},
            {"quality", "high"},          // low | medium | high | ultra
            {"encoder", "auto"},
            {"codec", "h264"},            // h264 | hevc
            {"cursor", false},
            {"clipSeconds", 30},          // seconds kept from before you press the hotkey
            {"postSeconds", 0},           // seconds kept recording after you press it (total is never over 20 minutes)
            {"sessionRecording", false},  // record every game session in full
        }},
        {"audio", QJsonObject{
            {"mode", "game"},             // game | voice | both | off
            {"micDevice", "default"},
            {"gameVolume", 100},
            {"micVolume", 100},
        }},
        {"clips", QJsonObject{{"folder", Paths::defaultClips()}}},
        {"hotkeys", QJsonObject{
            {"saveClip", "F8"},
            {"toggleRecording", "F9"},
            {"screenshot", "F10"},
            {"toggleBuffer", "Ctrl+F10"},
        }},
        {"highlights", QJsonObject{
            {"auto", true},               // save a clip by itself when you get loud (a hype moment)
            {"thresholdDb", -9},          // how loud counts as hype (dBFS peak)
        }},
        {"profiles", QJsonObject{}},      // per-game capture profiles, keyed by program file
        {"games", QJsonObject{{"include", QJsonArray{}}, {"exclude", QJsonArray{}}}},
        {"window", QJsonObject{{"geometry", ""}}},
    };
}

static QJsonObject merged(const QJsonObject &def, const QJsonObject &over) {
    QJsonObject out = def;
    for (auto it = over.begin(); it != over.end(); ++it) {
        if (it->isObject() && def.value(it.key()).isObject())
            out[it.key()] = merged(def.value(it.key()).toObject(), it->toObject());
        else
            out[it.key()] = *it;
    }
    return out;
}

bool Settings::load(const QString &path) {
    m_path = path;
    m_root = defaults();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    m_root = merged(defaults(), doc.object());
    return true;
}

bool Settings::save() const {
    if (m_path.isEmpty()) return false;
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(m_root).toJson(QJsonDocument::Indented));
    return f.commit();
}

void Settings::resetToDefaults() { m_root = defaults(); }

QVariant Settings::get(const QString &key) const {
    const QStringList parts = key.split('.');
    QJsonValue v = m_root;
    for (const auto &p : parts) {
        if (!v.isObject()) return {};
        v = v.toObject().value(p);
    }
    return v.toVariant();
}

static void setPath(QJsonObject &obj, QStringList parts, const QJsonValue &v) {
    const QString head = parts.takeFirst();
    if (parts.isEmpty()) { obj[head] = v; return; }
    QJsonObject child = obj.value(head).toObject();
    setPath(child, parts, v);
    obj[head] = child;
}

void Settings::set(const QString &key, const QVariant &value) {
    if (get(key) == value) return;
    setPath(m_root, key.split('.'), QJsonValue::fromVariant(value));
    Q_EMIT changed(key);
}

} // namespace kl
