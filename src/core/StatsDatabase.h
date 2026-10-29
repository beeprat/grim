#pragma once

#include "models/Track.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QSqlDatabase>
#include <QString>

struct TrackStat
{
    Track track;
    qint64 playCount = 0;
    qint64 listenedMs = 0;
};

struct ArtistStat
{
    QString artist;
    qint64 playCount = 0;
    qint64 listenedMs = 0;
};

struct AlbumStat
{
    QString album;
    QString artist;
    QString samplePath; // one of its tracks, to find the album in the library
    qint64 playCount = 0;
    qint64 listenedMs = 0;
};

struct StatsTotals
{
    qint64 playCount = 0;
    qint64 listenedMs = 0;
};

// Local SQLite store for listening history.
// Not thread-safe: use it only from the thread that called open().
class StatsDatabase
{
public:
    StatsDatabase();
    ~StatsDatabase();
    StatsDatabase(const StatsDatabase &) = delete;
    StatsDatabase &operator=(const StatsDatabase &) = delete;

    // Opens or creates the file and migrates the schema. Call once.
    // ":memory:" gives a private in-memory database.
    bool open(const QString &filePath);
    bool isOpen() const;

    // Records one listening session: the time actually spent playing, not the track length.
    // With playId, the first call inserts the session and stores its id there; later calls
    // with that id update the same session's time, so it can be saved while still playing.
    // Only sessions passing countsAsPlay() add to play counts.
    bool recordPlay(const Track &track, const QDateTime &startedAt, qint64 listenedMs, qint64 *playId = nullptr);

    StatsTotals totals() const;
    QList<TrackStat> topTracks(int limit) const;
    QList<ArtistStat> topArtists(int limit) const;
    QList<AlbumStat> topAlbums(int limit) const;
    // One artist's totals, name matched ignoring case.
    ArtistStat artistStat(const QString &artist) const;

    // Whole listening history as JSON: {"format": "grim-stats", "version": 2, "plays": [{path, file,
    // title, artist, album, durationMs, startedAt (epoch s), listenedMs, counted}]}. "file" is the
    // path inside libraryFolder with "/" (absent for files outside it), so an export from one
    // computer or system imports on another whose music folder is elsewhere. Empty on failure.
    QByteArray exportJson(const QString &libraryFolder = {}) const;
    // Adds the plays of an exportJson() file (version 1 or 2). Each play's file is found as "file"
    // under libraryFolder, else as the end of "path" that exists under libraryFolder (so version 1
    // exports from Windows match on Linux), else "path" as is. A play already here (same file and
    // start time) is skipped, so importing the same file twice changes nothing. All or nothing: on
    // any error nothing is added. Returns the number of plays added, or -1 if the file is not a
    // valid export.
    int importJson(const QByteArray &json, const QString &libraryFolder = {});

    // A play is 30 seconds of listening (streaming-service rule); a track shorter
    // than a minute counts after half of it.
    static bool countsAsPlay(qint64 durationMs, qint64 listenedMs);

private:
    QSqlDatabase database() const;
    bool migrate();

    QString m_connectionName;
};
