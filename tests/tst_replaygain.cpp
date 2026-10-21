#include "core/ReplayGain.h"

#include <fileref.h>
#include <tpropertymap.h>

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

bool writeSilentWav(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + 4);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(2) << quint32(44100) << quint32(44100 * 4) << quint16(4)
        << quint16(16);
    out.writeRawData("data", 4);
    out << quint32(4) << quint32(0);
    return out.status() == QDataStream::Ok;
}

bool near(float a, double b)
{
    return std::abs(a - b) < 1e-4;
}

} // namespace

class TestReplayGain : public QObject
{
    Q_OBJECT

private slots:
    void parsesNumbers()
    {
        QCOMPARE(ReplayGain::parseNumber(u"-6.48 dB"_s), -6.48);
        QCOMPARE(ReplayGain::parseNumber(u"+1.20 DB"_s), 1.2);
        QCOMPARE(ReplayGain::parseNumber(u" 0,5 "_s), 0.5);
        QCOMPARE(ReplayGain::parseNumber(u"0.988525"_s), 0.988525);
        QVERIFY(!ReplayGain::parseNumber(u""_s));
        QVERIFY(!ReplayGain::parseNumber(u"loud"_s));
    }

    void picksGainForMode()
    {
        ReplayGain::Info info;
        QVERIFY(near(ReplayGain::factor(info, ReplayGain::Track), 1)); // no tags: unchanged

        info.trackGainDb = -6.0;
        info.albumGainDb = -3.0;
        QVERIFY(near(ReplayGain::factor(info, ReplayGain::Off), 1));
        QVERIFY(near(ReplayGain::factor(info, ReplayGain::Track), std::pow(10.0, -6.0 / 20)));
        QVERIFY(near(ReplayGain::factor(info, ReplayGain::Album), std::pow(10.0, -3.0 / 20)));

        // Each mode falls back to the other gain when its own is missing.
        ReplayGain::Info albumOnly;
        albumOnly.albumGainDb = -3.0;
        QVERIFY(near(ReplayGain::factor(albumOnly, ReplayGain::Track), std::pow(10.0, -3.0 / 20)));
        ReplayGain::Info trackOnly;
        trackOnly.trackGainDb = -6.0;
        QVERIFY(near(ReplayGain::factor(trackOnly, ReplayGain::Album), std::pow(10.0, -6.0 / 20)));
    }

    void peakLimitsBoost()
    {
        ReplayGain::Info info;
        info.trackGainDb = 6.0; // would double the level
        info.trackPeak = 0.8;   // but the loudest sample may only grow by 1 / 0.8
        QVERIFY(near(ReplayGain::factor(info, ReplayGain::Track), 1.25));
    }

    void readsTags()
    {
        QTemporaryDir dir;
        const QString song = dir.filePath(u"Glóry.wav"_s);
        QVERIFY(writeSilentWav(song));
        QVERIFY(!ReplayGain::read(song).trackGainDb);
        {
            TagLib::FileRef file(QFileInfo(song).filesystemFilePath().c_str());
            TagLib::PropertyMap tags = file.properties();
            tags.insert("REPLAYGAIN_TRACK_GAIN", TagLib::String("-7.25 dB"));
            tags.insert("REPLAYGAIN_TRACK_PEAK", TagLib::String("0.977"));
            tags.insert("REPLAYGAIN_ALBUM_GAIN", TagLib::String("-6.50 dB"));
            file.setProperties(tags);
            QVERIFY(file.save());
        }
        const ReplayGain::Info info = ReplayGain::read(song);
        QCOMPARE(info.trackGainDb, -7.25);
        QCOMPARE(info.trackPeak, 0.977);
        QCOMPARE(info.albumGainDb, -6.5);
        QVERIFY(!info.albumPeak);
    }
};

QTEST_GUILESS_MAIN(TestReplayGain)
#include "tst_replaygain.moc"
