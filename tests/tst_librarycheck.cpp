#include "core/LibraryCheck.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {

Track track(const QString &path, const QString &album, int year, const QString &genre, int number)
{
    Track t;
    t.path = path;
    t.album = album;
    t.artist = u"Artist"_s;
    t.year = year;
    t.genre = genre;
    t.trackNumber = number;
    return t;
}

} // namespace

class TestLibraryCheck : public QObject
{
    Q_OBJECT

private slots:
    void listsAlbumsWithGaps()
    {
        const QList<Track> tracks{track(u"C:/m/a/1.flac"_s, u"Full"_s, 2001, u"Rock"_s, 1),
                                  track(u"C:/m/b/1.flac"_s, u"Gappy"_s, 2002, u"Pop"_s, 1),
                                  track(u"C:/m/b/2.flac"_s, u"gappy"_s, 0, u""_s, 2),
                                  track(u"C:/m/c/1.flac"_s, u"Bare"_s, 2003, u"Jazz"_s, 1)};
        const auto hasCover = [](const QString &path) { return !path.contains(u"/c/"_s); };
        const QList<LibraryCheck::Gap> gaps = LibraryCheck::missingTags(tracks, hasCover);
        QCOMPARE(gaps.size(), 2);
        QCOMPARE(gaps[0].album, u"Gappy"_s);
        QCOMPARE(gaps[0].paths.size(), 2);
        QVERIFY(gaps[0].year && gaps[0].genre && !gaps[0].cover && !gaps[0].trackNumber);
        QCOMPARE(gaps[1].album, u"Bare"_s);
        QVERIFY(gaps[1].cover && !gaps[1].year);
    }

    void groupsSameNameAndSize()
    {
        const QList<Track> tracks{track(u"C:/m/a/Song.mp3"_s, {}, 0, {}, 0), track(u"C:/m/b/song.MP3"_s, {}, 0, {}, 0),
                                  track(u"C:/m/c/Song.mp3"_s, {}, 0, {}, 0), track(u"C:/m/d/Other.mp3"_s, {}, 0, {}, 0)};
        const auto sizeOf = [](const QString &path) { return path.contains(u"/c/"_s) ? 5 : 10; };
        const QList<QStringList> groups = LibraryCheck::duplicates(tracks, sizeOf);
        QCOMPARE(groups.size(), 1);
        QCOMPARE(groups[0], (QStringList{u"C:/m/a/Song.mp3"_s, u"C:/m/b/song.MP3"_s}));
    }
};

QTEST_GUILESS_MAIN(TestLibraryCheck)
#include "tst_librarycheck.moc"
