#include "core/AudioConverter.h"
#include "core/TagReader.h"

#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>

#include <QFileInfo>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// One second of 16-bit stereo silence at the given rate.
bool writeSilentWav(const QString &path, quint32 sampleRate)
{
    constexpr quint16 kChannels = 2;
    constexpr quint16 kBytesPerFrame = kChannels * 2;
    const quint32 dataSize = sampleRate * kBytesPerFrame;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + dataSize);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << kChannels << sampleRate << quint32(sampleRate * kBytesPerFrame)
        << kBytesPerFrame << quint16(16);
    out.writeRawData("data", 4);
    out << dataSize;
    file.write(QByteArray(dataSize, '\0'));
    return out.status() == QDataStream::Ok;
}

std::filesystem::path native(const QString &path)
{
    return QFileInfo(path).filesystemFilePath();
}

} // namespace

class TestAudioConverter : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (AudioConverter::ffmpegPath().isEmpty())
            QSKIP("ffmpeg not on PATH");
    }

    void convertsAndKeepsTags_data()
    {
        QTest::addColumn<QString>("format");
        QTest::addColumn<quint32>("sampleRate");
        QTest::newRow("m4a 44.1 kHz") << u"m4a"_s << 44100u;
        QTest::newRow("m4a 192 kHz is resampled") << u"m4a"_s << 192000u;
        QTest::newRow("mp3 192 kHz is resampled") << u"mp3"_s << 192000u;
        QTest::newRow("flac 44.1 kHz") << u"flac"_s << 44100u;
    }

    void convertsAndKeepsTags()
    {
        QFETCH(QString, format);
        QFETCH(quint32, sampleRate);

        QTemporaryDir dir;
        const QString source = dir.filePath(u"Glóry Box.wav"_s);
        QVERIFY(writeSilentWav(source, sampleRate));
        {
            const std::filesystem::path path = native(source);
            TagLib::FileRef file(path.c_str());
            TagLib::PropertyMap tags;
            tags.insert("TITLE", TagLib::String(u"Glóry Box"_s.toStdWString()));
            tags.insert("ARTIST", TagLib::String("Portishead"));
            tags.insert("ALBUM", TagLib::String("Dummy"));
            tags.insert("REPLAYGAIN_TRACK_GAIN", TagLib::String("-7.21 dB"));
            file.setProperties(tags);
            QVERIFY(file.save());
        }

        AudioConverter converter;
        converter.setFormat(format);
        QSignalSpy finished(&converter, &AudioConverter::finished);
        converter.convert(source);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
        QVERIFY2(finished.first().at(2).toBool(), qPrintable(finished.first().at(3).toString()));

        const QString target = finished.first().at(1).toString();
        QCOMPARE(target, dir.filePath(u"Glóry Box."_s + format));
        QVERIFY(QFile::exists(source)); // original kept
        QVERIFY(!QFile::exists(dir.filePath(u"Glóry Box.converting."_s + format)));

        const Track track = TagReader::read(target);
        QCOMPARE(track.title, u"Glóry Box"_s);
        QCOMPARE(track.artist, u"Portishead"_s);
        QCOMPARE(track.album, u"Dummy"_s);
        QVERIFY(qAbs(track.durationMs - 1000) < 100);

        const std::filesystem::path targetPath = native(target);
        const TagLib::FileRef converted(targetPath.c_str());
        QCOMPARE(QString::fromStdWString(converted.properties()["REPLAYGAIN_TRACK_GAIN"].toString().toWString()),
                 u"-7.21 dB"_s);
        QCOMPARE(converted.audioProperties()->bitrate() > 0, true);

        // A second request for the same file finishes at once without re-encoding.
        converter.convert(source);
        QCOMPARE(finished.count(), 2);
        QVERIFY(finished.last().at(2).toBool());
    }

    void destroyingMidConversionLeavesNoPartialFile()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath(u"long.wav"_s);
        QVERIFY(writeSilentWav(source, 44100));

        {
            AudioConverter converter;
            QSignalSpy started(&converter, &AudioConverter::started);
            converter.convert(source);
            QCOMPARE(started.count(), 1); // ffmpeg launched, conversion in flight
        }
        QVERIFY(!QFile::exists(dir.filePath(u"long.converting.m4a"_s)));
        QVERIFY(!QFile::exists(dir.filePath(u"long.m4a"_s)));
    }

    void reportsFailureForNonAudio()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath(u"notes.flac"_s);
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not audio at all");
        file.close();

        AudioConverter converter;
        QSignalSpy finished(&converter, &AudioConverter::finished);
        converter.convert(source);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
        QVERIFY(!finished.first().at(2).toBool());
        QVERIFY(!finished.first().at(3).toString().isEmpty());
        QVERIFY(!QFile::exists(dir.filePath(u"notes.converting.m4a"_s)));
        QVERIFY(!QFile::exists(dir.filePath(u"notes.m4a"_s)));
    }
};

QTEST_GUILESS_MAIN(TestAudioConverter)
#include "tst_audioconverter.moc"
