#include "core/StatsDatabase.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QSqlError>
#include <QSqlQuery>
#include <QtLogging>

#include <algorithm>
#include <atomic>

using namespace Qt::StringLiterals;

namespace {

constexpr int kSchemaVersion = 2;

std::atomic<int> s_connectionCounter{0};

bool logFailure(const QSqlQuery &query)
{
    qWarning().noquote() << "StatsDatabase:" << query.lastError().text() << "in" << query.lastQuery();
    return false;
}

bool logFailure(const QSqlDatabase &db)
{
    qWarning().noquote() << "StatsDatabase:" << db.lastError().text();
    return false;
}

} // namespace

StatsDatabase::StatsDatabase()
    : m_connectionName(u"stats_%1"_s.arg(++s_connectionCounter))
{
}

StatsDatabase::~StatsDatabase()
{
    if (!QSqlDatabase::contains(m_connectionName))
        return;
    // The QSqlDatabase handle must be gone before removeDatabase().
    {
        QSqlDatabase db = database();
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

QSqlDatabase StatsDatabase::database() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

bool StatsDatabase::open(const QString &filePath)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, m_connectionName);
    db.setDatabaseName(filePath);
    if (!db.open()) {
        qWarning().noquote() << "StatsDatabase: cannot open" << filePath << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    if (!query.exec(u"PRAGMA foreign_keys = ON"_s) || !query.exec(u"PRAGMA journal_mode = WAL"_s))
        return logFailure(query);
    query.finish(); // journal_mode returns a row; an active statement blocks COMMIT
    return migrate();
}

bool StatsDatabase::isOpen() const
{
    return database().isOpen();
}

bool StatsDatabase::migrate()
{
    QSqlDatabase db = database();
    QSqlQuery query(db);
    if (!query.exec(u"PRAGMA user_version"_s) || !query.next())
        return logFailure(query);
    const int version = query.value(0).toInt();
    query.finish();

    if (version == kSchemaVersion)
        return true;
    if (version > kSchemaVersion) {
        qWarning() << "StatsDatabase: schema version" << version << "is newer than this build supports";
        return false;
    }

    // One list of statements per version step. Never edit a shipped step; append a new one.
    const QList<QStringList> steps = {
        // 0 -> 1
        {
            uR"(CREATE TABLE tracks (
                    id          INTEGER PRIMARY KEY,
                    path        TEXT    NOT NULL UNIQUE,
                    title       TEXT    NOT NULL DEFAULT '',
                    artist      TEXT    NOT NULL DEFAULT '',
                    album       TEXT    NOT NULL DEFAULT '',
                    duration_ms INTEGER NOT NULL DEFAULT 0
                ))"_s,
            uR"(CREATE TABLE plays (
                    id          INTEGER PRIMARY KEY,
                    track_id    INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
                    started_at  INTEGER NOT NULL,
                    listened_ms INTEGER NOT NULL CHECK (listened_ms > 0),
                    counted     INTEGER NOT NULL CHECK (counted IN (0, 1))
                ))"_s,
            u"CREATE INDEX plays_track_id ON plays(track_id)"_s,
        },
        // 1 -> 2: play rule changed from "half the track or 4 min" to countsAsPlay()'s 30 s rule.
        {
            uR"(UPDATE plays SET counted = (listened_ms >= (
                    SELECT CASE WHEN t.duration_ms > 0 AND t.duration_ms / 2 < 30000 THEN t.duration_ms / 2
                                ELSE 30000 END
                    FROM tracks t WHERE t.id = plays.track_id)))"_s,
        },
    };

    if (!db.transaction())
        return logFailure(db);
    for (int step = version; step < kSchemaVersion; ++step) {
        for (const QString &statement : steps.at(step)) {
            if (!query.exec(statement)) {
                logFailure(query);
                db.rollback();
                return false;
            }
        }
    }
    if (!query.exec(u"PRAGMA user_version = %1"_s.arg(kSchemaVersion))) {
        logFailure(query);
        db.rollback();
        return false;
    }
    return db.commit() || logFailure(db);
}

bool StatsDatabase::countsAsPlay(qint64 durationMs, qint64 listenedMs)
{
    constexpr qint64 kPlayThresholdMs = 30 * 1000;
    if (durationMs > 0 && durationMs / 2 < kPlayThresholdMs)
        return listenedMs >= durationMs / 2; // very short tracks: half of it
    return listenedMs >= kPlayThresholdMs;
}

bool StatsDatabase::recordPlay(const Track &track, const QDateTime &startedAt, qint64 listenedMs, qint64 *playId)
{
    if (track.path.isEmpty() || listenedMs <= 0 || !startedAt.isValid())
        return false;
    const int counted = countsAsPlay(track.durationMs, listenedMs) ? 1 : 0;

    // Later saves of the same session only move its listening time forward.
    if (playId && *playId > 0) {
        QSqlQuery update(database());
        update.prepare(u"UPDATE plays SET listened_ms = ?, counted = ? WHERE id = ?"_s);
        update.addBindValue(listenedMs);
        update.addBindValue(counted);
        update.addBindValue(*playId);
        return update.exec() || logFailure(update);
    }

    QSqlDatabase db = database();
    if (!db.transaction())
        return logFailure(db);

    QSqlQuery upsert(db);
    upsert.prepare(uR"(INSERT INTO tracks (path, title, artist, album, duration_ms)
                       VALUES (?, COALESCE(?, ''), COALESCE(?, ''), COALESCE(?, ''), ?)
                       ON CONFLICT(path) DO UPDATE SET
                           title = excluded.title, artist = excluded.artist,
                           album = excluded.album, duration_ms = excluded.duration_ms
                       RETURNING id)"_s);
    upsert.addBindValue(track.path);
    upsert.addBindValue(track.title);
    upsert.addBindValue(track.artist);
    upsert.addBindValue(track.album);
    upsert.addBindValue(track.durationMs);
    if (!upsert.exec() || !upsert.next()) {
        logFailure(upsert);
        db.rollback();
        return false;
    }
    const qint64 trackId = upsert.value(0).toLongLong();
    upsert.finish(); // an unfinished RETURNING statement blocks COMMIT

    QSqlQuery insert(db);
    insert.prepare(u"INSERT INTO plays (track_id, started_at, listened_ms, counted) VALUES (?, ?, ?, ?)"_s);
    insert.addBindValue(trackId);
    insert.addBindValue(startedAt.toSecsSinceEpoch());
    insert.addBindValue(listenedMs);
    insert.addBindValue(counted);
    if (!insert.exec()) {
        logFailure(insert);
        db.rollback();
        return false;
    }
    if (playId)
        *playId = insert.lastInsertId().toLongLong();
    return db.commit() || logFailure(db);
}

StatsTotals StatsDatabase::totals() const
{
    QSqlQuery query(database());
    if (!query.exec(u"SELECT COALESCE(SUM(counted), 0), COALESCE(SUM(listened_ms), 0) FROM plays"_s)
        || !query.next()) {
        logFailure(query);
        return {};
    }
    return {query.value(0).toLongLong(), query.value(1).toLongLong()};
}

// All rankings order by time actually listened; play counts are shown alongside.
QList<TrackStat> StatsDatabase::topTracks(int limit) const
{
    QSqlQuery query(database());
    query.prepare(uR"(SELECT t.path, t.title, t.artist, t.album, t.duration_ms,
                             SUM(p.counted) AS play_count, SUM(p.listened_ms) AS listened
                      FROM plays p JOIN tracks t ON t.id = p.track_id
                      GROUP BY t.id
                      ORDER BY listened DESC, play_count DESC
                      LIMIT ?)"_s);
    query.addBindValue(limit);
    if (!query.exec()) {
        logFailure(query);
        return {};
    }

    QList<TrackStat> result;
    while (query.next()) {
        Track track;
        track.path = query.value(0).toString();
        track.title = query.value(1).toString();
        track.artist = query.value(2).toString();
        track.album = query.value(3).toString();
        track.durationMs = query.value(4).toLongLong();
        result.append({track, query.value(5).toLongLong(), query.value(6).toLongLong()});
    }
    return result;
}

QList<ArtistStat> StatsDatabase::topArtists(int limit) const
{
    QSqlQuery query(database());
    query.prepare(uR"(SELECT t.artist, SUM(p.counted) AS play_count, SUM(p.listened_ms) AS listened
                      FROM plays p JOIN tracks t ON t.id = p.track_id
                      WHERE t.artist <> ''
                      GROUP BY t.artist
                      ORDER BY listened DESC, play_count DESC
                      LIMIT ?)"_s);
    query.addBindValue(limit);
    if (!query.exec()) {
        logFailure(query);
        return {};
    }

    QList<ArtistStat> result;
    while (query.next())
        result.append({query.value(0).toString(), query.value(1).toLongLong(), query.value(2).toLongLong()});
    return result;
}

ArtistStat StatsDatabase::artistStat(const QString &artist) const
{
    QSqlQuery query(database());
    query.prepare(uR"(SELECT SUM(p.counted), SUM(p.listened_ms)
                      FROM plays p JOIN tracks t ON t.id = p.track_id
                      WHERE t.artist = ? COLLATE NOCASE)"_s);
    query.addBindValue(artist);
    if (!query.exec()) {
        logFailure(query);
        return {artist, 0, 0};
    }
    if (!query.next())
        return {artist, 0, 0};
    return {artist, query.value(0).toLongLong(), query.value(1).toLongLong()};
}

QList<AlbumStat> StatsDatabase::topAlbums(int limit) const
{
    // An album is its name plus artist: two artists' "Greatest Hits" are two albums.
    QSqlQuery query(database());
    query.prepare(uR"(SELECT t.album, t.artist, MIN(t.path), SUM(p.counted) AS play_count, SUM(p.listened_ms) AS listened
                      FROM plays p JOIN tracks t ON t.id = p.track_id
                      WHERE t.album <> ''
                      GROUP BY t.album, t.artist
                      ORDER BY listened DESC, play_count DESC
                      LIMIT ?)"_s);
    query.addBindValue(limit);
    if (!query.exec()) {
        logFailure(query);
        return {};
    }

    QList<AlbumStat> result;
    while (query.next()) {
        result.append({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                       query.value(3).toLongLong(), query.value(4).toLongLong()});
    }
    return result;
}

namespace {

// "C:\\Music\\A\\b.mp3" or "/home/x/Music/A/b.mp3" → its path parts.
QStringList pathParts(const QString &path)
{
    return QString(path).replace(u'\\', u'/').split(u'/', Qt::SkipEmptyParts);
}

// Where an exported play's file is on this computer (see importJson).
QString resolveImported(const QJsonObject &play, const QString &libraryFolder, QHash<QString, QString> &cache)
{
    const QString path = play.value("path"_L1).toString();
    if (libraryFolder.isEmpty())
        return path;
    const QDir library(libraryFolder);
    const QString file = play.value("file"_L1).toString();
    if (!file.isEmpty())
        return QDir::cleanPath(library.filePath(file));
    if (QFileInfo::exists(path))
        return path;
    const auto known = cache.constFind(path);
    if (known != cache.cend())
        return known.value();
    // Drop leading folders until the rest exists in the library ("C:/Users/x/Music/A/b.mp3" →
    // "A/b.mp3"); at least the folder and file name must match.
    QString found = path;
    const QStringList parts = pathParts(path);
    for (qsizetype first = 0; first + 2 <= parts.size(); ++first) {
        const QString candidate = library.filePath(parts.mid(first).join(u'/'));
        if (QFileInfo::exists(candidate)) {
            found = QDir::cleanPath(candidate);
            break;
        }
    }
    cache.insert(path, found);
    return found;
}

} // namespace

QByteArray StatsDatabase::exportJson(const QString &libraryFolder) const
{
    const QString library = libraryFolder.isEmpty() ? QString() : QDir::cleanPath(libraryFolder) + u'/';
    QSqlQuery query(database());
    if (!query.exec(uR"(SELECT t.path, t.title, t.artist, t.album, t.duration_ms,
                               p.started_at, p.listened_ms, p.counted
                        FROM plays p JOIN tracks t ON t.id = p.track_id
                        ORDER BY p.started_at, p.id)"_s)) {
        logFailure(query);
        return {};
    }
    QJsonArray plays;
    while (query.next()) {
        const QString path = query.value(0).toString();
        QJsonObject play{
            {u"path"_s, path},
            {u"title"_s, query.value(1).toString()},
            {u"artist"_s, query.value(2).toString()},
            {u"album"_s, query.value(3).toString()},
            {u"durationMs"_s, query.value(4).toLongLong()},
            {u"startedAt"_s, query.value(5).toLongLong()},
            {u"listenedMs"_s, query.value(6).toLongLong()},
            {u"counted"_s, query.value(7).toInt() == 1},
        };
        const QString clean = QDir::cleanPath(QString(path).replace(u'\\', u'/'));
        if (!library.isEmpty() && clean.startsWith(library, Qt::CaseInsensitive))
            play.insert(u"file"_s, clean.mid(library.size()));
        plays.append(play);
    }
    return QJsonDocument(QJsonObject{{u"format"_s, u"grim-stats"_s}, {u"version"_s, 2}, {u"plays"_s, plays}})
        .toJson(QJsonDocument::Indented);
}

int StatsDatabase::importJson(const QByteArray &json, const QString &libraryFolder)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const int version = root.value("version"_L1).toInt();
    if (root.value("format"_L1).toString() != "grim-stats"_L1 || version < 1 || version > 2
        || !root.value("plays"_L1).isArray())
        return -1;
    const QJsonArray plays = root.value("plays"_L1).toArray();
    QHash<QString, QString> resolved; // exported path → file here

    QSqlDatabase db = database();
    if (!db.transaction()) {
        logFailure(db);
        return -1;
    }
    const auto fail = [&](const QSqlQuery &query) {
        logFailure(query);
        db.rollback();
        return -1;
    };

    QSqlQuery upsert(db);
    // A track already known keeps its current tags; the export only fills in unknown ones.
    upsert.prepare(uR"(INSERT INTO tracks (path, title, artist, album, duration_ms)
                       VALUES (?, COALESCE(?, ''), COALESCE(?, ''), COALESCE(?, ''), ?)
                       ON CONFLICT(path) DO UPDATE SET path = excluded.path
                       RETURNING id)"_s);
    QSqlQuery exists(db);
    exists.prepare(u"SELECT 1 FROM plays WHERE track_id = ? AND started_at = ?"_s);
    QSqlQuery insert(db);
    insert.prepare(u"INSERT INTO plays (track_id, started_at, listened_ms, counted) VALUES (?, ?, ?, ?)"_s);

    int added = 0;
    for (const QJsonValue &value : plays) {
        const QJsonObject play = value.toObject();
        const QString path = resolveImported(play, libraryFolder, resolved);
        const qint64 startedAt = play.value("startedAt"_L1).toInteger();
        const qint64 listenedMs = play.value("listenedMs"_L1).toInteger();
        if (path.isEmpty() || listenedMs <= 0) {
            db.rollback();
            return -1; // not a play this database could have written
        }
        upsert.addBindValue(path);
        upsert.addBindValue(play.value("title"_L1).toString());
        upsert.addBindValue(play.value("artist"_L1).toString());
        upsert.addBindValue(play.value("album"_L1).toString());
        upsert.addBindValue(play.value("durationMs"_L1).toInteger());
        if (!upsert.exec() || !upsert.next())
            return fail(upsert);
        const qint64 trackId = upsert.value(0).toLongLong();
        upsert.finish();

        exists.addBindValue(trackId);
        exists.addBindValue(startedAt);
        if (!exists.exec())
            return fail(exists);
        const bool known = exists.next();
        exists.finish();
        if (known)
            continue;

        insert.addBindValue(trackId);
        insert.addBindValue(startedAt);
        insert.addBindValue(listenedMs);
        insert.addBindValue(play.value("counted"_L1).toBool() ? 1 : 0);
        if (!insert.exec())
            return fail(insert);
        ++added;
    }
    if (!db.commit()) {
        logFailure(db);
        return -1;
    }
    return added;
}
