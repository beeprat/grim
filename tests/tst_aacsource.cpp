#include "audio/AacSource.h"
#include "core/AudioConverter.h"

#include <QFileInfo>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <vector>

using namespace Qt::StringLiterals;

namespace {

constexpr quint32 kRate = 44100;

// One second of a 16-bit stereo ramp, so decoded audio is not all silence.
bool writeWav(const QString &path)
{
    constexpr quint16 kChannels = 2;
    constexpr quint16 kBytesPerFrame = kChannels * 2;
    const quint32 dataSize = kRate * kBytesPerFrame;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + dataSize);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << kChannels << kRate << quint32(kRate * kBytesPerFrame) << kBytesPerFrame
        << quint16(16);
    out.writeRawData("data", 4);
    out << dataSize;
    for (quint32 i = 0; i < kRate; ++i) {
        const auto value = qint16((i % 200) * 100 - 10000);
        out << value << value;
    }
    return out.status() == QDataStream::Ok;
}

} // namespace

class TestAacSource : public QObject
{
    Q_OBJECT

private slots:
    void decodesAndSeeksM4a()
    {
        if (AudioConverter::ffmpegPath().isEmpty())
            QSKIP("ffmpeg not on PATH (needed to make the .m4a fixture)");

        QTemporaryDir dir;
        const QString wav = dir.filePath(u"ramp.wav"_s);
        QVERIFY(writeWav(wav));
        AudioConverter converter;
        QSignalSpy finished(&converter, &AudioConverter::finished);
        converter.convert(wav);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
        QVERIFY(finished.first().at(2).toBool());
        const QString m4a = finished.first().at(1).toString();

        const std::filesystem::path path = QFileInfo(m4a).filesystemFilePath();
        QVERIFY(AacSource::handles(path));
        const auto source = AacSource::open(path);
        QVERIFY(source);

        ma_format format;
        ma_uint32 channels = 0;
        ma_uint32 sampleRate = 0;
        QCOMPARE(ma_data_source_get_data_format(source->dataSource(), &format, &channels, &sampleRate, nullptr, 0),
                 MA_SUCCESS);
        QCOMPARE(format, ma_format_f32);
        QCOMPARE(channels, 2u);
        QCOMPARE(sampleRate, kRate);

        ma_uint64 length = 0;
        QCOMPARE(ma_data_source_get_length_in_pcm_frames(source->dataSource(), &length), MA_SUCCESS);
        QVERIFY2(length > kRate - 2048 && length < kRate + 2048, qPrintable(QString::number(length)));

        // Read everything: about one second, and not silent.
        std::vector<float> buffer(4096 * channels);
        ma_uint64 total = 0;
        float peak = 0;
        for (;;) {
            ma_uint64 read = 0;
            ma_data_source_read_pcm_frames(source->dataSource(), buffer.data(), 4096, &read);
            if (read == 0)
                break;
            for (ma_uint64 i = 0; i < read * channels; ++i)
                peak = std::max(peak, std::abs(buffer[i]));
            total += read;
        }
        QVERIFY2(total > kRate - 2048 && total < kRate + 4096, qPrintable(QString::number(total)));
        QVERIFY(peak > 0.1f);

        // Seek to the middle: cursor lands on the target and about half remains.
        QCOMPARE(ma_data_source_seek_to_pcm_frame(source->dataSource(), kRate / 2), MA_SUCCESS);
        ma_uint64 cursor = 0;
        ma_data_source_get_cursor_in_pcm_frames(source->dataSource(), &cursor);
        QCOMPARE(cursor, ma_uint64(kRate / 2));
        ma_uint64 rest = 0;
        for (;;) {
            ma_uint64 read = 0;
            ma_data_source_read_pcm_frames(source->dataSource(), buffer.data(), 4096, &read);
            if (read == 0)
                break;
            rest += read;
        }
        QVERIFY2(rest > kRate / 2 - 2048 && rest < kRate / 2 + 4096, qPrintable(QString::number(rest)));
    }

    void rejectsMissingFile()
    {
        QVERIFY(!AacSource::open("/does/not/exist.m4a"));
        QVERIFY(!AacSource::handles("song.flac"));
        QVERIFY(AacSource::handles("SONG.M4A"));
    }
};

QTEST_GUILESS_MAIN(TestAacSource)
#include "tst_aacsource.moc"
