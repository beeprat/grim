#include "core/TagReader.h"

#include <fileref.h>
#include <tag.h>

#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <tpropertymap.h>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Writes one second of 16-bit stereo 44.1 kHz silence as a PCM WAV file.
bool writeSilentWav(const QString &path)
{
    constexpr quint32 kSampleRate = 44100;
    constexpr quint16 kChannels = 2;
    constexpr quint16 kBytesPerFrame = kChannels * 2;
    constexpr quint32 kDataSize = kSampleRate * kBytesPerFrame;

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

class TestTagReader : public QObject
{
    Q_OBJECT

private slots:
    void readsTagsAndDuration()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"tagged ä.wav"_s);
        QVERIFY(writeSilentWav(path));
        {
            TagLib::FileRef file(path.toStdWString().c_str());
            QVERIFY(!file.isNull());
            file.tag()->setTitle(TagLib::String(u"Hoppípolla"_s.toStdWString()));
            file.tag()->setArtist(TagLib::String(u"Sigur Rós"_s.toStdWString()));
            file.tag()->setAlbum(TagLib::String(u"Takk..."_s.toStdWString()));
            file.tag()->setYear(2005);
            file.tag()->setGenre(TagLib::String("Post-rock"));
            file.tag()->setTrack(7);
            QVERIFY(file.save());
        }

        const Track track = TagReader::read(path);
        QCOMPARE(track.title, u"Hoppípolla"_s);
        QCOMPARE(track.artist, u"Sigur Rós"_s);
        QCOMPARE(track.album, u"Takk..."_s);
        QCOMPARE(track.year, 2005);
        QCOMPARE(track.genre, u"Post-rock"_s);
        QCOMPARE(track.trackNumber, 7);
        QCOMPARE(track.durationMs, 1000);
        QCOMPARE(track.path, path);
    }

    void untaggedFallsBackToFileName()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"some sóng.wav"_s);
        QVERIFY(writeSilentWav(path));

        const Track track = TagReader::read(path);
        QCOMPARE(track.title, u"some sóng"_s);
        QVERIFY(track.artist.isEmpty());
        QCOMPARE(track.durationMs, 1000);
    }

    void readsEmbeddedCoverThenFolderImage()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"song.wav"_s);
        QVERIFY(writeSilentWav(path));
        QVERIFY(TagReader::readCover(path).isEmpty());

        // Folder image is the fallback.
        QFile folderImage(dir.filePath(u"Cover.JPG"_s));
        QVERIFY(folderImage.open(QIODevice::WriteOnly));
        folderImage.write("folder-jpeg");
        folderImage.close();
        QCOMPARE(TagReader::readCover(path), "folder-jpeg");

        // Embedded picture wins over the folder image.
        {
            TagLib::FileRef file(path.toStdWString().c_str());
            TagLib::VariantMap picture;
            picture.insert("data", TagLib::ByteVector("embedded-png", 12));
            picture.insert("mimeType", TagLib::String("image/png"));
            picture.insert("pictureType", TagLib::String("Front Cover"));
            QVERIFY(file.setComplexProperties("PICTURE", {picture}));
            QVERIFY(file.save());
        }
        QCOMPARE(TagReader::readCover(path), "embedded-png");
    }

    void readsDiscNumber()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"disc.wav"_s);
        QVERIFY(writeSilentWav(path));
        {
            TagLib::FileRef file(path.toStdWString().c_str());
            TagLib::PropertyMap tags = file.properties();
            tags.insert("DISCNUMBER", TagLib::String("2/3"));
            file.setProperties(tags);
            QVERIFY(file.save());
        }
        QCOMPARE(TagReader::read(path).discNumber, 2);
    }

    void albumOrder()
    {
        auto make = [](int disc, int number, const QString &path) {
            Track track;
            track.discNumber = disc;
            track.trackNumber = number;
            track.path = path;
            return track;
        };
        // File names would sort these alphabetically ("Because" first); tags put them in album order.
        QList<Track> tracks{make(0, 0, u"bonus.m4a"_s), make(2, 1, u"d2-1.m4a"_s), make(1, 10, u"Because.m4a"_s),
                            make(0, 2, u"Something.m4a"_s), make(1, 1, u"Come Together.m4a"_s)};
        std::sort(tracks.begin(), tracks.end(), albumOrderLess);
        QStringList order;
        for (const Track &track : tracks)
            order << track.path;
        QCOMPARE(order, (QStringList{u"Come Together.m4a"_s, u"Something.m4a"_s, u"Because.m4a"_s, u"bonus.m4a"_s,
                                     u"d2-1.m4a"_s}));
    }

    void missingFileStillGivesTrack()
    {
        const Track track = TagReader::read(u"C:/does/not/exist/ghost.mp3"_s);
        QCOMPARE(track.title, u"ghost"_s);
        QCOMPARE(track.durationMs, 0);
    }
};

QTEST_GUILESS_MAIN(TestTagReader)
#include "tst_tagreader.moc"
