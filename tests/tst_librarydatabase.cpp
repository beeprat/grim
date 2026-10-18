#include "core/LibraryDatabase.h"

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
            QVERIFY(db.createPlaylist(u"P"_s, {u"x"_s}) > 0);
        }
        LibraryDatabase db;
        QVERIFY(db.open(file));
        QCOMPARE(db.rating(u"x"_s), 5); // loaded into the cache on open
        QVERIFY(db.isFavorite(u"x"_s));
        QCOMPARE(db.playlistTracks(db.playlists().first().id), QStringList{u"x"_s});
    }
};

QTEST_GUILESS_MAIN(TestLibraryDatabase)
#include "tst_librarydatabase.moc"
