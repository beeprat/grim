#include "core/DownloadTagger.h"
#include "core/TagReader.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// One second of 16-bit stereo silence as a PCM WAV file (TagLib tags WAV through ID3v2).
bool writeSilentWav(const QString &path)
{
    constexpr quint32 kSampleRate = 44100;
    constexpr quint16 kChannels = 2;
    constexpr quint16 kBytesPerFrame = kChannels * 2;
    constexpr quint32 kDataSize = kSampleRate * kBytesPerFrame;

    QDir().mkpath(QFileInfo(path).absolutePath());
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

// Bytes that start like a JPEG; the tagger does not decode covers.
QByteArray fakeJpeg(char fill)
{
    return QByteArray("\xFF\xD8\xFF\xE0", 4) + QByteArray(64, fill);
}

Track track(const QString &path, const QString &artist, const QString &album, int year)
{
    Track t;
    t.path = path;
    t.title = QFileInfo(path).completeBaseName();
    t.artist = artist;
    t.album = album;
    t.year = year;
    return t;
}

} // namespace

class TestDownloadTagger : public QObject
{
    Q_OBJECT

private slots:
    void cleansEditionNotesFromTitles_data()
    {
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("expected");
        QTest::newRow("bracket year") << u"Help! (Remastered 2009)"_s << u"Help!"_s;
        QTest::newRow("bracket remaster") << u"Help! (2015 Remaster)"_s << u"Help!"_s;
        QTest::newRow("square deluxe") << u"Help! [Deluxe Edition]"_s << u"Help!"_s;
        QTest::newRow("dash remastered") << u"Help! - Remastered 2009"_s << u"Help!"_s;
        QTest::newRow("dash remaster") << u"Help! - 2015 Remaster"_s << u"Help!"_s;
        QTest::newRow("en dash deluxe") << u"Help! – Deluxe"_s << u"Help!"_s;
        QTest::newRow("case") << u"Help! (REMASTERED)"_s << u"Help!"_s;
        QTest::newRow("live stays") << u"Help! (Live)"_s << u"Help! (Live)"_s;
        QTest::newRow("dash title stays") << u"Ob-La-Di, Ob-La-Da"_s << u"Ob-La-Di, Ob-La-Da"_s;
        QTest::newRow("remastered word alone stays") << u"Remastered"_s << u"Remastered"_s;
    }

    void cleansEditionNotesFromTitles()
    {
        QFETCH(QString, title);
        QFETCH(QString, expected);
        QCOMPARE(DownloadTagger::cleanTitle(title), expected);
    }

    void dropsTrackNumbersFromFileNames_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("track");
        QTest::addColumn<QString>("expected");
        QTest::newRow("space") << u"01 Help!.m4a"_s << 1 << u"Help!.m4a"_s;
        QTest::newRow("dash") << u"01 - Help!.m4a"_s << 1 << u"Help!.m4a"_s;
        QTest::newRow("dot") << u"01. Help!.m4a"_s << 1 << u"Help!.m4a"_s;
        QTest::newRow("disc-track") << u"1-07 Help!.m4a"_s << 7 << u"Help!.m4a"_s;
        QTest::newRow("padded, no tag") << u"07 Help!.m4a"_s << 0 << u"Help!.m4a"_s;
        QTest::newRow("title number") << u"99 Problems.mp3"_s << 0 << u"99 Problems.mp3"_s;
        QTest::newRow("title number, other track") << u"99 Problems.mp3"_s << 4 << u"99 Problems.mp3"_s;
        QTest::newRow("only number") << u"01.mp3"_s << 1 << u"01.mp3"_s;
        QTest::newRow("none") << u"Help!.m4a"_s << 1 << u"Help!.m4a"_s;
    }

    void dropsTrackNumbersFromFileNames()
    {
        QFETCH(QString, name);
        QFETCH(int, track);
        QFETCH(QString, expected);
        QCOMPARE(DownloadTagger::cleanFileName(name, track), expected);
    }

    void anniversaryEditionJoinsTheOriginalAlbum()
    {
        const QList<Track> library = {
            track(u"C:/Music/Abbey Road/a.m4a"_s, u"The Beatles"_s, u"Abbey Road"_s, 1969),
            track(u"C:/Music/Abbey Road/b.m4a"_s, u"The Beatles"_s, u"Abbey Road"_s, 1969),
        };
        const Track download = track(u"C:/D/Something.mp3"_s, u"The Beatles"_s,
                                     u"Abbey Road (Super Deluxe 50th Anniversary Edition)"_s, 2019);
        const DownloadTagger::AlbumMatch match = DownloadTagger::findAlbum(download, library);
        QVERIFY(match.found);
        QCOMPARE(match.album, u"Abbey Road"_s);
        QCOMPARE(match.year, 1969);
        QCOMPARE(DownloadTagger::cleanTitle(u"Something (2019 Mix) [50th Anniversary]"_s), u"Something (2019 Mix)"_s);
    }

    void albumKeyIgnoresReleaseExtras_data()
    {
        QTest::addColumn<QString>("album");
        QTest::newRow("plain") << u"Abbey Road"_s;
        QTest::newRow("super deluxe bracket") << u"Abbey Road (Super Deluxe Edition)"_s;
        QTest::newRow("anniversary square") << u"Abbey Road [50th Anniversary Super Deluxe]"_s;
        QTest::newRow("expanded") << u"Abbey Road (Expanded)"_s;
        QTest::newRow("bonus track version dash") << u"Abbey Road - Bonus Track Version"_s;
        QTest::newRow("trailing words") << u"Abbey Road Super Deluxe Edition"_s;
        QTest::newRow("trailing year + remaster") << u"Abbey Road 2019 Remaster"_s;
        QTest::newRow("colon") << u"Abbey Road: Special Edition"_s;
    }

    void albumKeyIgnoresReleaseExtras()
    {
        QFETCH(QString, album);
        QCOMPARE(DownloadTagger::albumKey(album), u"abbey road"_s);
    }

    void albumKeyKeepsRealNames()
    {
        QCOMPARE(DownloadTagger::albumKey(u"1999"_s), u"1999"_s);
        QCOMPARE(DownloadTagger::albumKey(u"Blink-182"_s), u"blink 182"_s);       // a number alone stays
        QCOMPARE(DownloadTagger::albumKey(u"Deluxe"_s), u"deluxe"_s);            // the whole name
        QCOMPARE(DownloadTagger::albumKey(u"Help! (Live)"_s), u"help live"_s);
        QVERIFY(DownloadTagger::albumKey(u"Let It Be... Naked"_s) != DownloadTagger::albumKey(u"Let It Be"_s));
    }

    void findsTheFolderMostOfTheAlbumIsIn()
    {
        const QList<Track> library = {
            track(u"C:/Music/Beatles/Rubber Soul/01 Drive My Car.m4a"_s, u"The Beatles"_s, u"Rubber Soul"_s, 1965),
            track(u"C:/Music/Beatles/Rubber Soul/02 Norwegian Wood.m4a"_s, u"The Beatles"_s, u"Rubber Soul"_s, 1965),
            track(u"C:/Music/Beatles/Rubber Soul/03 You Won't See Me.m4a"_s, u"The Beatles"_s, u"Rubber Soul"_s, 0),
            // A stray copy elsewhere, with a different year: outvoted.
            track(u"C:/Music/Misc/04 Nowhere Man.m4a"_s, u"The Beatles"_s, u"Rubber Soul"_s, 2009),
            // Another artist's album with the same name: not this album.
            track(u"C:/Music/Other/Rubber Soul/01 x.m4a"_s, u"Someone Else"_s, u"Rubber Soul"_s, 2020),
        };
        // Downloads often carry edition notes and a slightly different artist spelling.
        const Track download = track(u"C:/Downloads/Nowhere Man.mp3"_s, u"Beatles"_s,
                                     u"Rubber Soul (Remastered 2009)"_s, 2009);

        const DownloadTagger::AlbumMatch match = DownloadTagger::findAlbum(download, library);
        QVERIFY(match.found);
        QCOMPARE(QDir::fromNativeSeparators(match.folder), u"C:/Music/Beatles/Rubber Soul"_s);
        QCOMPARE(match.artist, u"The Beatles"_s);
        QCOMPARE(match.album, u"Rubber Soul"_s);
        QCOMPARE(match.year, 1965);
        QCOMPARE(match.paths.size(), 3);
    }

    void findsNothingWithoutAnAlbumOrForAnotherArtist()
    {
        const QList<Track> library = {
            track(u"C:/Music/A/01.m4a"_s, u"The Beatles"_s, u"Help!"_s, 1965),
        };
        QVERIFY(!DownloadTagger::findAlbum(track(u"C:/D/x.mp3"_s, u"The Beatles"_s, QString(), 0), library).found);
        QVERIFY(!DownloadTagger::findAlbum(track(u"C:/D/x.mp3"_s, u"Oasis"_s, u"Help!"_s, 0), library).found);
        QVERIFY(DownloadTagger::findAlbum(track(u"C:/D/x.mp3"_s, QString(), u"help"_s, 0), library).found);
    }

    void writesTheMajorityCoverAndTags()
    {
        QTemporaryDir dir;
        const QString album = dir.filePath(u"album"_s);
        const QByteArray common = fakeJpeg('a');
        const QByteArray odd = fakeJpeg('b');
        QStringList paths;
        for (int i = 1; i <= 3; ++i) {
            const QString path = u"%1/%2.wav"_s.arg(album).arg(i);
            QVERIFY(writeSilentWav(path));
            QVERIFY(DownloadTagger::writeTags(path, QString(), QString(), QString(), 0, i == 3 ? odd : common));
            paths.append(path);
        }
        QCOMPARE(DownloadTagger::majorityCover(paths), common);

        const QString download = dir.filePath(u"download/Nowhere Man.wav"_s);
        QVERIFY(writeSilentWav(download));
        QVERIFY(DownloadTagger::writeTags(download, u"Nowhere Man"_s, u"The Beatles"_s, u"Rubber Soul"_s, 1965, common));
        const Track tags = TagReader::read(download);
        QCOMPARE(tags.title, u"Nowhere Man"_s);
        QCOMPARE(tags.artist, u"The Beatles"_s);
        QCOMPARE(tags.album, u"Rubber Soul"_s);
        QCOMPARE(tags.year, 1965);
        QCOMPARE(TagReader::readCover(download), common);
    }
};

QTEST_GUILESS_MAIN(TestDownloadTagger)
#include "tst_downloadtagger.moc"
