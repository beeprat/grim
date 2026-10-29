#include "core/StatsDatabase.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTest>
#include <QTimeZone>

using namespace Qt::StringLiterals;

namespace {

Track makeTrack(const QString &path, const QString &artist, qint64 durationMs = 200'000)
{
    Track track;
    track.path = path;
    track.title = u"Title "_s + path;
    track.artist = artist;
    track.album = u"Album"_s;
    track.durationMs = durationMs;
    return track;
}

const QDateTime kStart = QDateTime::fromSecsSinceEpoch(1'700'000'000, QTimeZone::UTC);

} // namespace

class TestStatsDatabase : public QObject
{
    Q_OBJECT

private slots:
    void countsAsPlay_data()
    {
        QTest::addColumn<qint64>("durationMs");
        QTest::addColumn<qint64>("listenedMs");
        QTest::addColumn<bool>("expected");
        QTest::newRow("30 s of a long track") << 1'200'000LL << 30'000LL << true;
        QTest::newRow("29 s of a long track") << 1'200'000LL << 29'999LL << false;
        QTest::newRow("half of a 40 s track") << 40'000LL << 20'000LL << true;
        QTest::newRow("under half of a 40 s track") << 40'000LL << 19'999LL << false;
        QTest::newRow("unknown duration, 30 s") << 0LL << 30'000LL << true;
        QTest::newRow("unknown duration, 29 s") << 0LL << 29'999LL << false;
    }

    void countsAsPlay()
    {
        QFETCH(qint64, durationMs);
        QFETCH(qint64, listenedMs);
        QFETCH(bool, expected);
        QCOMPARE(StatsDatabase::countsAsPlay(durationMs, listenedMs), expected);
    }

    void aggregatesPlays()
    {
        StatsDatabase db;
        QVERIFY(db.open(u":memory:"_s));
        const Track a = makeTrack(u"a.flac"_s, u"Artist A"_s);
        const Track b = makeTrack(u"b.flac"_s, u"Artist B"_s);

        QVERIFY(db.recordPlay(a, kStart, 200'000));
        QVERIFY(db.recordPlay(a, kStart, 150'000));
        QVERIFY(db.recordPlay(b, kStart, 120'000));
        QVERIFY(db.recordPlay(b, kStart, 10'000)); // under 30 s: time only, not a play

        const StatsTotals totals = db.totals();
        QCOMPARE(totals.playCount, 3);
        QCOMPARE(totals.listenedMs, 480'000);

        const QList<TrackStat> tracks = db.topTracks(10);
        QCOMPARE(tracks.size(), 2);
        QCOMPARE(tracks[0].track.path, u"a.flac"_s);
        QCOMPARE(tracks[0].playCount, 2);
        QCOMPARE(tracks[1].playCount, 1);
        QCOMPARE(tracks[1].listenedMs, 130'000);

        const QList<ArtistStat> artists = db.topArtists(1);
        QCOMPARE(artists.size(), 1);
        QCOMPARE(artists[0].artist, u"Artist A"_s);
    }

    void updatesOneSessionWhilePlaying()
    {
        StatsDatabase db;
        QVERIFY(db.open(u":memory:"_s));
        const Track track = makeTrack(u"a.flac"_s, u"A"_s);

        qint64 playId = 0;
        QVERIFY(db.recordPlay(track, kStart, 5'000, &playId));
        QVERIFY(playId > 0);
        QCOMPARE(db.totals().playCount, 0); // 5 s is not a play yet
        QVERIFY(db.recordPlay(track, kStart, 35'000, &playId));

        const StatsTotals totals = db.totals();
        QCOMPARE(totals.listenedMs, 35'000); // updated, not added: still one session
        QCOMPARE(totals.playCount, 1);
        QCOMPARE(db.topTracks(10).size(), 1);
    }

    void ranksByListeningTimeAndGroupsAlbums()
    {
        StatsDatabase db;
        QVERIFY(db.open(u":memory:"_s));
        Track shortHit = makeTrack(u"hit.flac"_s, u"Pop"_s);
        shortHit.album = u"Singles"_s;
        Track longSong = makeTrack(u"epic.flac"_s, u"Prog"_s);
        longSong.album = u"Epic"_s;
        Track sameNameOtherArtist = makeTrack(u"other.flac"_s, u"Rock"_s);
        sameNameOtherArtist.album = u"Singles"_s;

        // Three short plays still rank below one long listen.
        for (int i = 0; i < 3; ++i)
            QVERIFY(db.recordPlay(shortHit, kStart, 40'000));
        QVERIFY(db.recordPlay(longSong, kStart, 600'000));
        QVERIFY(db.recordPlay(sameNameOtherArtist, kStart, 50'000));

        QCOMPARE(db.topTracks(10).first().track.path, u"epic.flac"_s);
        QCOMPARE(db.topArtists(10).first().artist, u"Prog"_s);

        const QList<AlbumStat> albums = db.topAlbums(10);
        QCOMPARE(albums.size(), 3); // "Singles" by Pop and by Rock stay separate
        QCOMPARE(albums[0].album, u"Epic"_s);
        QCOMPARE(albums[1].album, u"Singles"_s);
        QCOMPARE(albums[1].artist, u"Pop"_s);
        QCOMPARE(albums[1].playCount, 3);
        QCOMPARE(albums[1].samplePath, u"hit.flac"_s);
        QCOMPARE(albums[1].listenedMs, 120'000);
    }

    void migratesVersion1PlayCounts()
    {
        // A v1 database counted 35 s of a 200 s track as no play; v2 counts it.
        QTemporaryDir dir;
        const QString path = dir.filePath(u"v1.sqlite"_s);
        {
            QSqlDatabase v1 = QSqlDatabase::addDatabase(u"QSQLITE"_s, u"v1"_s);
            v1.setDatabaseName(path);
            QVERIFY(v1.open());
            QSqlQuery q(v1);
            QVERIFY(q.exec(u"CREATE TABLE tracks (id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, title TEXT NOT NULL DEFAULT '', artist TEXT NOT NULL DEFAULT '', album TEXT NOT NULL DEFAULT '', duration_ms INTEGER NOT NULL DEFAULT 0)"_s));
            QVERIFY(q.exec(u"CREATE TABLE plays (id INTEGER PRIMARY KEY, track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, started_at INTEGER NOT NULL, listened_ms INTEGER NOT NULL CHECK (listened_ms > 0), counted INTEGER NOT NULL CHECK (counted IN (0, 1)))"_s));
            QVERIFY(q.exec(u"INSERT INTO tracks (id, path, duration_ms) VALUES (1, 'a.flac', 200000)"_s));
            QVERIFY(q.exec(u"INSERT INTO plays (track_id, started_at, listened_ms, counted) VALUES (1, 0, 35000, 0), (1, 0, 5000, 0)"_s));
            QVERIFY(q.exec(u"PRAGMA user_version = 1"_s));
            q.finish();
            v1.close();
        }
        QSqlDatabase::removeDatabase(u"v1"_s);

        StatsDatabase db;
        QVERIFY(db.open(path));
        QCOMPARE(db.totals().playCount, 1);
        QCOMPARE(db.totals().listenedMs, 40'000);
    }

    void excludesEmptyArtistButKeepsShortListens()
    {
        StatsDatabase db;
        QVERIFY(db.open(u":memory:"_s));
        QVERIFY(db.recordPlay(makeTrack(u"x.mp3"_s, QString()), kStart, 200'000));
        QVERIFY(db.recordPlay(makeTrack(u"y.mp3"_s, u"Y"_s), kStart, 1'000));

        const QList<ArtistStat> artists = db.topArtists(10);
        QCOMPARE(artists.size(), 1); // the untagged track has no artist row
        QCOMPARE(artists[0].artist, u"Y"_s);
        QCOMPARE(artists[0].playCount, 0); // listened time counts even without a full play
        QCOMPARE(db.topTracks(10).size(), 2);
    }

    void updatesTrackMetadata()
    {
        StatsDatabase db;
        QVERIFY(db.open(u":memory:"_s));
        Track track = makeTrack(u"a.flac"_s, u"Old"_s);
        QVERIFY(db.recordPlay(track, kStart, 200'000));
        track.artist = u"New"_s;
        QVERIFY(db.recordPlay(track, kStart, 200'000));

        const QList<TrackStat> tracks = db.topTracks(10);
        QCOMPARE(tracks.size(), 1);
        QCOMPARE(tracks[0].track.artist, u"New"_s);
        QCOMPARE(tracks[0].playCount, 2);
    }

    void rejectsInvalidInput()
    {
        StatsDatabase db;
        QVERIFY(db.open(u":memory:"_s));
        QVERIFY(!db.recordPlay(makeTrack(QString(), u"A"_s), kStart, 1'000));
        QVERIFY(!db.recordPlay(makeTrack(u"a.flac"_s, u"A"_s), kStart, 0));
        QVERIFY(!db.recordPlay(makeTrack(u"a.flac"_s, u"A"_s), QDateTime(), 1'000));
        QCOMPARE(db.totals().listenedMs, 0);
    }

    void importsAcrossSystems()
    {
        // Exported on Windows with the music in C:/Users/x/Music; imported where it is elsewhere.
        QTemporaryDir music;
        const QString song = music.filePath(u"Camel/1. Mirage (1974)/Lady Fantasy.m4a"_s);
        QVERIFY(QDir().mkpath(QFileInfo(song).path()));
        QFile file(song);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        StatsDatabase source;
        QVERIFY(source.open(u":memory:"_s));
        QVERIFY(source.recordPlay(makeTrack(u"C:/Users/x/Music/Camel/1. Mirage (1974)/Lady Fantasy.m4a"_s, u"Camel"_s),
                                  kStart, 60'000));
        const QByteArray json = source.exportJson(u"C:/Users/x/Music"_s);
        QVERIFY(json.contains("\"file\": \"Camel/1. Mirage (1974)/Lady Fantasy.m4a\""));

        StatsDatabase target;
        QVERIFY(target.open(u":memory:"_s));
        QCOMPARE(target.importJson(json, music.path()), 1);
        QCOMPARE(target.topTracks(1).first().track.path, QDir::cleanPath(song));

        // A version 1 export (no "file", Windows path) finds the same file by the end of its path.
        QByteArray v1 = json;
        v1.replace("\"version\": 2", "\"version\": 1");
        v1.replace("\"file\": \"Camel/1. Mirage (1974)/Lady Fantasy.m4a\",", "");
        v1.replace("C:/Users/x/Music", "C:\\\\Users\\\\x\\\\Music");
        QVERIFY(!v1.contains("\"file\"") && v1.contains("\"version\": 1") && v1.contains("\\\\Users"));
        StatsDatabase old;
        QVERIFY(old.open(u":memory:"_s));
        QCOMPARE(old.importJson(v1, music.path()), 1);
        QCOMPARE(old.topTracks(1).first().track.path, QDir::cleanPath(song));
    }

    void exportsAndImportsWithoutDuplicates()
    {
        StatsDatabase source;
        QVERIFY(source.open(u":memory:"_s));
        QVERIFY(source.recordPlay(makeTrack(u"/m/a.mp3"_s, u"Camel"_s), kStart, 60'000));
        QVERIFY(source.recordPlay(makeTrack(u"/m/b.mp3"_s, u"Camel"_s), kStart.addSecs(300), 10'000));
        const QByteArray json = source.exportJson();
        QVERIFY(!json.isEmpty());

        StatsDatabase target;
        QVERIFY(target.open(u":memory:"_s));
        QVERIFY(target.recordPlay(makeTrack(u"/m/a.mp3"_s, u"Camel"_s), kStart, 60'000)); // already here
        QCOMPARE(target.importJson(json), 1);
        QCOMPARE(target.totals().playCount, 1); // the 10 s play does not count as a play
        QCOMPARE(target.totals().listenedMs, 70'000);

        // Importing again adds nothing; a foreign or broken file is refused.
        QCOMPARE(target.importJson(json), 0);
        QCOMPARE(target.importJson("{\"format\": \"other\"}"), -1);
        QCOMPARE(target.importJson("not json"), -1);
        QCOMPARE(target.totals().listenedMs, 70'000);
    }

    void persistsAcrossReopen()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(u"stats.sqlite"_s);
        {
            StatsDatabase db;
            QVERIFY(db.open(path));
            QVERIFY(db.recordPlay(makeTrack(u"a.flac"_s, u"A"_s), kStart, 200'000));
        }
        StatsDatabase db;
        QVERIFY(db.open(path)); // migration must be a no-op on an existing schema
        QCOMPARE(db.totals().playCount, 1);
    }
};

QTEST_GUILESS_MAIN(TestStatsDatabase)
#include "tst_statsdatabase.moc"
