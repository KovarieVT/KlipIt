// Engine test: the REAL capture engine end to end — startup, instant replay
// save, post-event timing, full-session recording split into parts,
// screenshots, and trimming/splitting the results.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "core/ClipLibrary.h"
#include "core/GameDetector.h"
#include "core/Logger.h"
#include "core/MediaTools.h"
#include "core/Settings.h"
#include "engine/Recorder.h"

using namespace kl;

class EngineTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        Q_INIT_RESOURCE(resources);
        QVERIFY(m_tmp.isValid());
        qputenv("APPDATA", m_tmp.path().toUtf8());
        qputenv("KLIPIT_DATA", m_tmp.path().toUtf8());
        qputenv("KLIPIT_TEST_SOURCE", "color_source_v3");
        qputenv("KLIPIT_TEST_CANVAS", "1280x720");
        qputenv("KLIPIT_TEST_SPLIT_SECONDS", "4");
        auto &s = Settings::instance();
        s.load(m_tmp.filePath("config.json"));
        s.set("clips.folder", m_tmp.filePath("clips"));
        s.set("capture.mode", "screen");
        s.set("capture.fps", 30);
        s.set("capture.quality", "low");
        s.set("capture.encoder", "obs_x264");
        s.set("capture.clipSeconds", 4);
        s.set("audio.mode", "off");
        s.set("highlights.auto", false);
        ClipLibrary::instance().setRoot(m_tmp.filePath("clips"));
        Logger::installObsLogHandler();
        QString err;
        QVERIFY2(Recorder::instance().startup({}, &err), qPrintable(err));
    }

    void bufferRunsInScreenMode() {
        auto &r = Recorder::instance();
        QTRY_VERIFY_WITH_TIMEOUT(r.bufferActive(), 10000);
        QVERIFY(!r.encoderName().isEmpty());
        QCOMPARE(r.canvasSize(), QSize(1280, 720));
    }

    void instantReplaySavesAClip() {
        auto &r = Recorder::instance();
        QTest::qWait(6000);   // fill the 4-second buffer
        QSignalSpy saved(&r, &Recorder::clipSaved);
        r.saveClip();
        QVERIFY(saved.wait(15000));
        const QString path = saved.first().at(0).toString();
        QVERIFY(QFile::exists(path));
        QVERIFY(path.contains("/Desktop/"));
        const media::Info info = media::probe(path);
        QVERIFY2(info.ok, qPrintable(info.error));
        qInfo("clip: %s  %.2fs  %dx%d", qPrintable(path), info.duration, info.width, info.height);
        QVERIFY(info.duration > 2.5 && info.duration < 7.0);
        QCOMPARE(info.width, 1280);
        QVERIFY(ClipLibrary::instance().find(path));
        m_clip = path;
    }

    void postEventKeepsRecording() {
        auto &r = Recorder::instance();
        Settings::instance().set("capture.postSeconds", 2);
        QSignalSpy saved(&r, &Recorder::clipSaved);
        QElapsedTimer t; t.start();
        r.saveClip(true);
        QVERIFY(saved.wait(15000));
        QVERIFY(t.elapsed() >= 1900);                     // waited for the post-event seconds
        QCOMPARE(saved.first().at(2).toBool(), true);     // marked as a highlight
        QVERIFY(ClipLibrary::instance().find(saved.first().at(0).toString())->highlight);
        Settings::instance().set("capture.postSeconds", 0);
    }

    void sessionRecordingSplitsIntoParts() {
        auto &r = Recorder::instance();
        QSignalSpy parts(&r, &Recorder::recordingSaved);
        r.startRecording(true);
        QVERIFY(r.recordingActive());
        QTest::qWait(11000);                              // 4-second parts in this test (20 minutes for real)
        r.stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT(!r.recordingActive(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(parts.count() >= 2, 20000);
        double total = 0;
        for (const auto &args : parts) {
            const QString p = args.at(0).toString();
            const media::Info info = media::probe(p);
            QVERIFY2(info.ok, qPrintable(p));
            QVERIFY(p.endsWith(".mp4"));
            QVERIFY(info.duration <= 6.5);                // a part never runs far past its limit
            total += info.duration;
            qInfo("part: %s  %.2fs", qPrintable(p), info.duration);
        }
        QVERIFY2(total > 8.0, "parts together must hold the whole session");
    }

    void screenshot() {
        auto &r = Recorder::instance();
        QSignalSpy shot(&r, &Recorder::screenshotSaved);
        r.takeScreenshot();
        QCOMPARE(shot.count(), 1);
        QImage img(shot.first().at(0).toString());
        QCOMPARE(img.size(), QSize(1280, 720));
    }

    void trimSavesANewClip() {
        const QString dst = QFileInfo(m_clip).absolutePath() + "/trimmed.mp4";
        QString err;
        QVERIFY2(media::cut(m_clip, dst, 1.0, 3.0, &err), qPrintable(err));
        const media::Info info = media::probe(dst);
        QVERIFY(info.ok);
        qInfo("trim: %.2fs", info.duration);
        QVERIFY(info.duration > 1.4 && info.duration < 3.6);
        QVERIFY(QFile::exists(m_clip));                   // the original is kept
        const QStringList parts = media::splitIntoParts(m_clip, m_tmp.filePath("parts"), "split", &err, {}, 2.0);
        QVERIFY2(parts.size() >= 2, qPrintable(err));
    }

    void cleanupTestCase() { Recorder::instance().shutdown(); }

private:
    QTemporaryDir m_tmp;
    QString m_clip;
};

QTEST_MAIN(EngineTest)
#include "test_engine.moc"
