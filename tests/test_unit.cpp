// Unit tests: settings, hotkeys, clip-length rules, game detection helpers and the clip tools.
#include <QtTest>
#include <QTemporaryDir>
#include "core/ClipLibrary.h"
#include "core/GameDetector.h"
#include "core/HotkeyManager.h"
#include "core/MediaTools.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "core/Theme.h"
#include "engine/Recorder.h"

using namespace kl;

class UnitTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        QVERIFY(m_tmp.isValid());
        qputenv("APPDATA", m_tmp.path().toUtf8());
        qputenv("KLIPIT_DATA", m_tmp.path().toUtf8());
        Settings::instance().load(m_tmp.filePath("config.json"));
    }

    void settingsDefaultsAndRoundTrip() {
        auto &s = Settings::instance();
        QCOMPARE(s.s("appearance.theme"), QString("black"));
        QCOMPARE(s.i("capture.clipSeconds"), 30);
        QCOMPARE(s.s("hotkeys.saveClip"), QString("F8"));
        s.set("capture.clipSeconds", 45);
        QVERIFY(s.save());
        Settings::instance().load(m_tmp.filePath("config.json"));
        QCOMPARE(s.i("capture.clipSeconds"), 45);
        QCOMPARE(s.s("audio.mode"), QString("game"));   // defaults still filled in
        s.set("capture.clipSeconds", 30);
    }

    void fiveThemes() {
        QStringList ids;
        for (const auto &p : theme::palettes()) ids << p.id;
        QCOMPARE(ids, (QStringList{"black", "darkgrey", "white", "blue", "purple"}));
        QCOMPARE(theme::byId("nope").id, QString("black"));
    }

    void hotkeyRules() {
        auto &h = HotkeyManager::instance();
        h.registerAction("saveClip", "Save clip", [] {});
        h.registerAction("toggleRecording", "Record", [] {});
        QCOMPARE(HotkeyManager::toVirtualKey(Qt::Key_F8), 0x77u);
        QCOMPARE(HotkeyManager::toVirtualKey(Qt::Key_Shift), 0u);
        QCOMPARE(HotkeyManager::toVirtualKey(Qt::Key_Asterisk, true), 0x6Au);     // numpad *
        QCOMPARE(HotkeyManager::toVirtualKey(Qt::Key_5, true), 0x65u);            // numpad 5
        // old settings ("F8") still read correctly
        QCOMPARE(h.binding("saveClip").vk, 0x77u);
        QCOMPARE(Hotkey::parse("Ctrl+F10").mods, 2u);
        QCOMPARE(Hotkey::parse("Num+*").vk, 0x6Au);
        // ANY key: numpad asterisk, with and without modifiers, round-trips through the settings file
        const Hotkey star = Hotkey::fromKey(0x6A, 0);
        QCOMPARE(star.text, QString("Num *"));
        QCOMPARE(Hotkey::parse(star.stored()).vk, 0x6Au);
        QCOMPARE(Hotkey::fromKey(0x6A, 2 | 4).text, QString("Ctrl+Shift+Num *"));
        QCOMPARE(Hotkey::fromKey(0xB3, 0).text, QString("Play/Pause"));
        QCOMPARE(Hotkey::fromKey(0x7C, 0).text, QString("F13"));
        QVERIFY(!h.conflictFor("toggleRecording", Hotkey::fromKey(0x77, 0)).isEmpty());   // F8 is Save clip
        QVERIFY(h.conflictFor("toggleRecording", Hotkey::fromKey(0x77, 2)).isEmpty());    // Ctrl+F8 is free
        h.suspend(true);
        QVERIFY(h.isSuspended());
        QVERIFY(h.setBinding("toggleRecording", star).isEmpty());
        QVERIFY(h.binding("toggleRecording").sameKeys(star));
        QVERIFY(!h.conflictFor("saveClip", star).isEmpty());
        h.suspend(false);
        QVERIFY(!h.isSuspended());
    }

    void gameNamesAndObsWindowString() {
        QCOMPARE(GameDetector::friendlyName("VALORANT-Win64-Shipping.exe", "VALORANT"), QString("VALORANT"));
        QCOMPARE(GameDetector::friendlyName("FortniteClient-Win64-Shipping.exe", "Unreal Engine"), QString("FortniteClient"));
        QVERIFY(GameDetector::isExcludedByDefault("discord.exe"));
        QVERIFY(GameDetector::isExcludedByDefault("chrome.exe"));
        QVERIFY(!GameDetector::isExcludedByDefault("gta5.exe"));
        GameInfo g; g.title = "Game: Edition #1"; g.windowClass = "UnrealWindow"; g.exe = "game.exe";
        QCOMPARE(g.obsWindowString(), QString("Game#3A Edition #221:UnrealWindow:game.exe"));
    }

    void clipLengthNeverOverTwentyMinutes() {
        auto &s = Settings::instance();
        s.set("capture.clipSeconds", 1200);
        s.set("capture.postSeconds", 60);
        QCOMPARE(Recorder::instance().clipSeconds(), 1200);   // post-event is squeezed to fit
        s.set("capture.clipSeconds", 9999);
        QCOMPARE(Recorder::instance().preSeconds(), 1200);
        s.set("capture.clipSeconds", 30);
        s.set("capture.postSeconds", 5);
        QCOMPARE(Recorder::instance().clipSeconds(), 35);
        s.set("capture.postSeconds", 0);
    }

    void gameProfilesOverride() {
        auto &s = Settings::instance();
        GameInfo g; g.pid = 1; g.exe = "Valorant.exe"; g.name = "VALORANT";
        s.set(Recorder::profileKey(g.exe) + ".quality", "ultra");
        QCOMPARE(Recorder::profileKey("Valorant.exe"), QString("profiles.valorant_exe"));
        QCOMPARE(Recorder::instance().effective("quality").toString(), QString("high"));   // no game running
    }

    void bitrateTable() {
        QVERIFY(Recorder::bitrateKbps(1440, 60, "high") > Recorder::bitrateKbps(1080, 60, "high"));
        QVERIFY(Recorder::bitrateKbps(1080, 30, "high") < Recorder::bitrateKbps(1080, 60, "high"));
        QVERIFY(Recorder::bitrateKbps(1080, 60, "low") < Recorder::bitrateKbps(1080, 60, "ultra"));
    }

    void safeNames() {
        QCOMPARE(safeFileName("Halo: Infinite?"), QString("Halo Infinite"));
        QCOMPARE(safeFileName("..."), QString("Clip"));
    }

    void cutRefusesOverTwentyMinutes() {
        QString err;
        QVERIFY(!media::cut("missing.mp4", m_tmp.filePath("x.mp4"), 0, 1201, &err));
        QVERIFY(err.contains("20 minutes"));
        QCOMPARE(media::formatDuration(65), QString("1:05"));
        QCOMPARE(media::formatDuration(3725), QString("1:02:05"));
    }

private:
    QTemporaryDir m_tmp;
};

QTEST_MAIN(UnitTest)
#include "test_unit.moc"
