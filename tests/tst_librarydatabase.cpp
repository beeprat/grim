#include "core/LibraryDatabase.h"
#include "core/LibrarySidecar.h"

#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestLibraryDatabase : public QObject
{
    Q_OBJECT

private slots:
    void ratingsAndFavorites()
    {
        LibraryDatabase db;
        QVERIFY(db.open(u":memory:"_s));
        const QString a = u"C:/Music/a.m4a"_s;
        const QString b = u"C:/Music/b.m4a"_s;

        QCOMPARE(db.rating(a), 0);
        QVERIFY(db.setRating(a, 4));
        QCOMPARE(db.rating(a), 4);
        QVERIFY(!db.setRating(a, 6)); // out of range
        QVERIFY(!db.setRating(a, -1));
        QCOMPARE(db.rating(a), 4);

        QVERIFY(!db.isFavorite(a));
        QVERIFY(db.setFavorite(a, true));
        QVERIFY(db.setFavorite(b, true));
        QVERIFY(db.isFavorite(a));
        QCOMPARE(db.rating(a), 4); // favoriting keeps the rating
        QCOMPARE(db.favorites().size(), 2);

        QVERIFY(db.setFavorite(a, false));
        QCOMPARE(db.favorites(), QStringList{b});
        QVERIFY(db.setRating(a, 0)); // clearing is allowed
        QCOMPARE(db.rating(a), 0);
    }

    void playlists()
    {
        LibraryDatabase db;
        QVERIFY(db.open(u":memory:"_s));
        QCOMPARE(db.createPlaylist(u"   "_s, {}), 0); // name required

        const qint64 rock = db.createPlaylist(u"Rock"_s, {u"1"_s, u"2"_s, u"3"_s});
        QVERIFY(rock > 0);
        const qint64 calm = db.createPlaylist(u"calm"_s, {});
        QVERIFY(calm > 0);

        const QList<Playlist> all = db.playlists();
        QCOMPARE(all.size(), 2);
        QCOMPARE(all[0].name, u"calm"_s); // sorted by name, case-insensitive
        QCOMPARE(all[0].trackCount, 0);
        QCOMPARE(all[1].trackCount, 3);

        QVERIFY(db.addToPlaylist(rock, {u"4"_s, u"2"_s})); // duplicates are allowed
        QCOMPARE(db.playlistTracks(rock), (QStringList{u"1"_s, u"2"_s, u"3"_s, u"4"_s, u"2"_s}));

        QVERIFY(db.removeFromPlaylist(rock, 1));
        QCOMPARE(db.playlistTracks(rock), (QStringList{u"1"_s, u"3"_s, u"4"_s, u"2"_s}));
        QVERIFY(db.removeFromPlaylist(rock, 0));
        QVERIFY(db.addToPlaylist(rock, {u"5"_s})); // positions stayed dense, so appending works
        QCOMPARE(db.playlistTracks(rock), (QStringList{u"3"_s, u"4"_s, u"2"_s, u"5"_s}));

        QVERIFY(db.renamePlaylist(calm, u"Chill"_s));
        QVERIFY(db.deletePlaylist(rock));
        QCOMPARE(db.playlists().size(), 1);
        QCOMPARE(db.playlists().first().name, u"Chill"_s);
        QVERIFY(db.playlistTracks(rock).isEmpty()); // tracks deleted with it
    }

    void persistsAcrossReopen()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(u"library.sqlite"_s);
        {
            LibraryDatabase db;
            QVERIFY(db.open(file));
            QVERIFY(db.setRating(u"x"_s, 5));
            QVERIFY(db.setFavorite(u"x"_s, true));
            QVERIFY(db.setReplayGainDb(u"x"_s, -4.5));
            QVERIFY(!db.setReplayGainDb(u"x"_s, 25.0)); // outside ±20 dB
            QVERIFY(db.setReplayGainDb(u"z"_s, 3.0));
            QVERIFY(db.setReplayGainDb(u"z"_s, std::nullopt)); // back to the tags
            QVERIFY(db.setRating(u"y"_s, 1));
            QVERIFY(db.createPlaylist(u"P"_s, {u"x"_s}) > 0);
        }
        LibraryDatabase db;
        QVERIFY(db.open(file));
        QCOMPARE(db.rating(u"x"_s), 5); // loaded into the cache on open
        QVERIFY(db.isFavorite(u"x"_s));
        QCOMPARE(db.replayGainDb(u"x"_s), -4.5);
        QCOMPARE(db.rating(u"x"_s), 5); // the dB write kept the rating
        QVERIFY(!db.replayGainDb(u"y"_s)); // the tags apply
        QVERIFY(!db.replayGainDb(u"z"_s));
        QCOMPARE(db.playlistTracks(db.playlists().first().id), QStringList{u"x"_s});
    }
    // Two computers: the first writes the sidecar into the music folder, the second (other folder path,
    // own ratings) reads it and keeps what it already had.
    void sidecarCarriesMetaToAnotherFolder()
    {
        QTemporaryDir first;
        QTemporaryDir second;
        const QString a = first.path() + u"/Artist/a.m4a"_s;
        const QString b = first.path() + u"/b.m4a"_s;
        LibraryDatabase source;
        QVERIFY(source.open(u":memory:"_s));
        QVERIFY(source.setRating(a, 5));
        QVERIFY(source.setReplayGainDb(a, -3.5));
        QVERIFY(source.setFavorite(b, true));
        QVERIFY(source.setFavorite(a, true));
        QVERIFY(source.setRating(u"D:/Elsewhere/c.m4a"_s, 2)); // outside the folder: left out
        QVERIFY(source.createPlaylist(u"Road"_s, {b, a}));
        QVERIFY(LibrarySidecar::write(source, first.path()));
        QVERIFY(QFile::copy(first.filePath(QLatin1StringView(LibrarySidecar::kFileName)),
                            second.filePath(QLatin1StringView(LibrarySidecar::kFileName))));

        const QString a2 = second.path() + u"/Artist/a.m4a"_s;
        const QString b2 = second.path() + u"/b.m4a"_s;
        LibraryDatabase target;
        QVERIFY(target.open(u":memory:"_s));
        QVERIFY(target.setRating(b2, 1)); // own rating stays
        QVERIFY(LibrarySidecar::read(target, second.path()));
        QCOMPARE(target.rating(a2), 5);
        QCOMPARE(target.rating(b2), 1);
        QCOMPARE(target.replayGainDb(a2), std::optional<double>(-3.5));
        QVERIFY(target.isFavorite(a2));
        QVERIFY(target.isFavorite(b2));
        QCOMPARE(target.playlists().size(), 1);
        QCOMPARE(target.playlistTracks(target.playlists().first().id), (QStringList{b2, a2}));
        QVERIFY(!target.rating(second.path() + u"/c.m4a"_s));

        QVERIFY(!LibrarySidecar::read(target, second.path())); // nothing new the second time
    }
};

QTEST_GUILESS_MAIN(TestLibraryDatabase)
#include "tst_librarydatabase.moc"
