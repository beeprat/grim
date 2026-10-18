#include "audio/AudioEngine.h"

#include <QFileInfo>
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// Two seconds of 16-bit stereo silence.
bool writeSilentWav(const QString &path)
{
    constexpr quint32 kSampleRate = 44100;
    constexpr quint16 kChannels = 2;
    constexpr quint16 kBytesPerFrame = kChannels * 2;
    constexpr quint32 kDataSize = 2 * kSampleRate * kBytesPerFrame;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + kDataSize);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << kChannels << kSampleRate << quint32(kSampleRate * kBytesPerFrame)
        << kBytesPerFrame << quint16(16);
    out.writeRawData("data", 4);
    out << kDataSize;
    file.write(QByteArray(kDataSize, '\0'));
    return out.status() == QDataStream::Ok;
}

} // namespace

class TestAudioEngine : public QObject
{
    Q_OBJECT

private slots:
    void crossfadeSwitchesTrackAndFreesOldOne()
    {
        AudioEngine engine;
        if (!engine.isReady())
            QSKIP("no audio output device");

        QTemporaryDir dir;
        const QString first = dir.filePath(u"a.wav"_s);
        const QString second = dir.filePath(u"b.wav"_s);
        QVERIFY(writeSilentWav(first));
        QVERIFY(writeSilentWav(second));

        QVERIFY(engine.load(QFileInfo(first).filesystemFilePath()));
        engine.play();
        QTRY_VERIFY(engine.positionMs() > 100);

        // The new track starts at 0 while the old one fades out for 300 ms.
        QVERIFY(engine.load(QFileInfo(second).filesystemFilePath(), 300));
        engine.play();
        QVERIFY(engine.isPlaying());
        QVERIFY(engine.positionMs() < 100);
        QCOMPARE(engine.durationMs(), 2000);

        // After the fade, update() frees the old track; the new one keeps playing.
        QTest::qWait(500);
        engine.update();
        QVERIFY(engine.isPlaying());
        QVERIFY(engine.positionMs() > 300);

        // A crossfade load when nothing plays is a plain load.
        engine.pause();
        QVERIFY(engine.load(QFileInfo(first).filesystemFilePath(), 300));
        QVERIFY(!engine.isPlaying());
        QCOMPARE(engine.positionMs(), 0);
    }

    void queuedTrackTakesOverAtTheEnd()
    {
        AudioEngine engine;
        if (!engine.isReady())
            QSKIP("no audio output device");

        QTemporaryDir dir;
        const QString first = dir.filePath(u"a.wav"_s);
        const QString second = dir.filePath(u"b.wav"_s);
        QVERIFY(writeSilentWav(first));
        QVERIFY(writeSilentWav(second));

        // Nothing is queued while paused.
        QVERIFY(engine.load(QFileInfo(first).filesystemFilePath()));
        QVERIFY(!engine.queueNext(QFileInfo(second).filesystemFilePath()));

        engine.play();
        QTRY_VERIFY(engine.positionMs() > 100);
        QVERIFY(engine.queueNext(QFileInfo(second).filesystemFilePath()));
        QVERIFY(engine.hasQueued());

        // A seek drops the queued track (it was timed for the old position); queue again.
        engine.seek(1500);
        QVERIFY(!engine.hasQueued());
        QVERIFY(engine.queueNext(QFileInfo(second).filesystemFilePath()));

        // When the first track ends, the second one is already playing from its start.
        QTRY_VERIFY_WITH_TIMEOUT(engine.atEnd(), 3000);
        QVERIFY(engine.promoteQueued());
        QVERIFY(engine.isPlaying());
        QVERIFY(engine.positionMs() < 500);
        QCOMPARE(engine.durationMs(), 2000);
        QVERIFY(!engine.hasQueued());
    }
};

QTEST_GUILESS_MAIN(TestAudioEngine)
#include "tst_audioengine.moc"
