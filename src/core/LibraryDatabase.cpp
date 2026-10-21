#include "core/LibraryDatabase.h"
#include "core/ReplayGain.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QtLogging>

#include <algorithm>
#include <cmath>
#include <atomic>

using namespace Qt::StringLiterals;

namespace {

constexpr int kSchemaVersion = 3;

std::atomic<int> s_connectionCounter{0};

bool logFailure(const QSqlQuery &query)
{
    qWarning().noquote() << "LibraryDatabase:" << query.lastError().text() << "in" << query.lastQuery();
    return false;
}

bool logFailure(const QSqlDatabase &db)
{
    qWarning().noquote() << "LibraryDatabase:" << db.lastError().text();
    return false;
}

} // namespace

LibraryDatabase::LibraryDatabase()
    : m_connectionName(u"library_%1"_s.arg(++s_connectionCounter))
{
}

LibraryDatabase::~LibraryDatabase()
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

QSqlDatabase LibraryDatabase::database() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

bool LibraryDatabase::open(const QString &filePath)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, m_connectionName);
    db.setDatabaseName(filePath);
    if (!db.open()) {
        qWarning().noquote() << "LibraryDatabase: cannot open" << filePath << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    if (!query.exec(u"PRAGMA foreign_keys = ON"_s) || !query.exec(u"PRAGMA journal_mode = WAL"_s))
        return logFailure(query);
    query.finish(); // journal_mode returns a row; an active statement blocks COMMIT
    if (!migrate())
        return false;

    if (!query.exec(u"SELECT path, rating, favorite, replaygain_db FROM track_meta"_s))
        return logFailure(query);
    while (query.next()) {
        const QVariant ownDb = query.value(3);
        m_meta.insert(query.value(0).toString(), {query.value(1).toInt(), query.value(2).toBool(),
                                                  ownDb.isNull() ? std::nullopt : std::optional(ownDb.toDouble())});
    }
    return true;
}

bool LibraryDatabase::isOpen() const
{
    return database().isOpen();
}

bool LibraryDatabase::migrate()
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
        qWarning() << "LibraryDatabase: schema version" << version << "is newer than this build supports";
        return false;
    }

    // One list of statements per version step. Never edit a shipped step; append a new one.
    const QList<QStringList> steps = {
        // 0 -> 1
        {
            uR"(CREATE TABLE track_meta (
                    path         TEXT    PRIMARY KEY,
                    rating       INTEGER NOT NULL DEFAULT 0 CHECK (rating BETWEEN 0 AND 5),
                    favorite     INTEGER NOT NULL DEFAULT 0 CHECK (favorite IN (0, 1)),
                    favorited_at INTEGER NOT NULL DEFAULT 0
                ))"_s,
            uR"(CREATE TABLE playlists (
                    id         INTEGER PRIMARY KEY,
                    name       TEXT    NOT NULL,
                    created_at INTEGER NOT NULL
                ))"_s,
            uR"(CREATE TABLE playlist_tracks (
                    playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
                    position    INTEGER NOT NULL,
                    path        TEXT    NOT NULL,
                    PRIMARY KEY (playlist_id, position)
                ))"_s,
        },
        // 1 -> 2: per-song ReplayGain mode, -1 = follow the setting
        {
            u"ALTER TABLE track_meta ADD COLUMN replaygain INTEGER NOT NULL DEFAULT -1 CHECK (replaygain BETWEEN -1 AND 2)"_s,
        },
        // 2 -> 3: the per-song mode became a per-song volume adjustment in dB, NULL = use the tags
        {
            u"ALTER TABLE track_meta DROP COLUMN replaygain"_s,
            u"ALTER TABLE track_meta ADD COLUMN replaygain_db REAL"_s,
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

int LibraryDatabase::rating(const QString &path) const
{
    return m_meta.value(path).rating;
}

bool LibraryDatabase::isFavorite(const QString &path) const
{
    return m_meta.value(path).favorite;
}

bool LibraryDatabase::setRating(const QString &path, int stars)
{
    if (path.isEmpty() || stars < 0 || stars > 5)
        return false;
    Meta meta = m_meta.value(path);
    meta.rating = stars;
    return saveMeta(path, meta);
}

bool LibraryDatabase::setFavorite(const QString &path, bool favorite)
{
    if (path.isEmpty())
        return false;
    Meta meta = m_meta.value(path);
    meta.favorite = favorite;
    return saveMeta(path, meta);
}

std::optional<double> LibraryDatabase::replayGainDb(const QString &path) const
{
    return m_meta.value(path).replayGainDb;
}

bool LibraryDatabase::setReplayGainDb(const QString &path, std::optional<double> db)
{
    if (path.isEmpty() || (db && !(std::abs(*db) <= ReplayGain::kMaxAdjustDb)))
        return false;
    Meta meta = m_meta.value(path);
    meta.replayGainDb = db;
    return saveMeta(path, meta);
}

bool LibraryDatabase::saveMeta(const QString &path, const Meta &meta)
{
    QSqlQuery query(database());
    query.prepare(uR"(INSERT INTO track_meta (path, rating, favorite, favorited_at, replaygain_db) VALUES (?, ?, ?, ?, ?)
                      ON CONFLICT(path) DO UPDATE SET
                          rating = excluded.rating,
                          favorite = excluded.favorite,
                          replaygain_db = excluded.replaygain_db,
                          favorited_at = CASE WHEN excluded.favorite AND NOT track_meta.favorite
                                              THEN excluded.favorited_at ELSE track_meta.favorited_at END)"_s);
    query.addBindValue(path);
    query.addBindValue(meta.rating);
    query.addBindValue(meta.favorite ? 1 : 0);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    query.addBindValue(meta.replayGainDb ? QVariant(*meta.replayGainDb) : QVariant(QMetaType::fromType<double>()));
    if (!query.exec())
        return logFailure(query);
    m_meta.insert(path, meta);
    return true;
}

QStringList LibraryDatabase::favorites() const
{
    QSqlQuery query(database());
    if (!query.exec(u"SELECT path FROM track_meta WHERE favorite = 1 ORDER BY favorited_at DESC, path"_s)) {
        logFailure(query);
        return {};
    }
    QStringList paths;
    while (query.next())
        paths.append(query.value(0).toString());
    return paths;
}

QList<Playlist> LibraryDatabase::playlists() const
{
    QSqlQuery query(database());
    if (!query.exec(uR"(SELECT p.id, p.name, COUNT(t.path)
                        FROM playlists p LEFT JOIN playlist_tracks t ON t.playlist_id = p.id
                        GROUP BY p.id
                        ORDER BY p.name COLLATE NOCASE)"_s)) {
        logFailure(query);
        return {};
    }
    QList<Playlist> result;
    while (query.next())
        result.append({query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toInt()});
    return result;
}

qint64 LibraryDatabase::createPlaylist(const QString &name, const QStringList &paths)
{
    if (name.trimmed().isEmpty())
        return 0;
    QSqlDatabase db = database();
    if (!db.transaction()) {
        logFailure(db);
        return 0;
    }
    QSqlQuery insert(db);
    insert.prepare(u"INSERT INTO playlists (name, created_at) VALUES (?, ?)"_s);
    insert.addBindValue(name.trimmed());
    insert.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!insert.exec()) {
        logFailure(insert);
        db.rollback();
        return 0;
    }
    const qint64 id = insert.lastInsertId().toLongLong();
    if (!db.commit()) {
        logFailure(db);
        return 0;
    }
    if (!paths.isEmpty() && !addToPlaylist(id, paths))
        return 0;
    return id;
}

bool LibraryDatabase::renamePlaylist(qint64 id, const QString &name)
{
    if (name.trimmed().isEmpty())
        return false;
    QSqlQuery query(database());
    query.prepare(u"UPDATE playlists SET name = ? WHERE id = ?"_s);
    query.addBindValue(name.trimmed());
    query.addBindValue(id);
    return query.exec() || logFailure(query);
}

bool LibraryDatabase::deletePlaylist(qint64 id)
{
    QSqlQuery query(database());
    query.prepare(u"DELETE FROM playlists WHERE id = ?"_s); // tracks go with ON DELETE CASCADE
    query.addBindValue(id);
    return query.exec() || logFailure(query);
}

QStringList LibraryDatabase::playlistTracks(qint64 id) const
{
    QSqlQuery query(database());
    query.prepare(u"SELECT path FROM playlist_tracks WHERE playlist_id = ? ORDER BY position"_s);
    query.addBindValue(id);
    if (!query.exec()) {
        logFailure(query);
        return {};
    }
    QStringList paths;
    while (query.next())
        paths.append(query.value(0).toString());
    return paths;
}

bool LibraryDatabase::addToPlaylist(qint64 id, const QStringList &paths)
{
    QSqlDatabase db = database();
    if (!db.transaction())
        return logFailure(db);

    QSqlQuery next(db);
    next.prepare(u"SELECT COALESCE(MAX(position) + 1, 0) FROM playlist_tracks WHERE playlist_id = ?"_s);
    next.addBindValue(id);
    if (!next.exec() || !next.next()) {
        logFailure(next);
        db.rollback();
        return false;
    }
    qint64 position = next.value(0).toLongLong();
    next.finish();

    QSqlQuery insert(db);
    insert.prepare(u"INSERT INTO playlist_tracks (playlist_id, position, path) VALUES (?, ?, ?)"_s);
    for (const QString &path : paths) {
        insert.addBindValue(id);
        insert.addBindValue(position++);
        insert.addBindValue(path);
        if (!insert.exec()) {
            logFailure(insert);
            db.rollback();
            return false;
        }
    }
    return db.commit() || logFailure(db);
}

bool LibraryDatabase::removeFromPlaylist(qint64 id, int position)
{
    // Positions stay dense (0..n-1): delete, then shift everything after it down by one.
    QSqlDatabase db = database();
    if (!db.transaction())
        return logFailure(db);
    QSqlQuery query(db);
    query.prepare(u"DELETE FROM playlist_tracks WHERE playlist_id = ? AND position = ?"_s);
    query.addBindValue(id);
    query.addBindValue(position);
    if (!query.exec()) {
        logFailure(query);
        db.rollback();
        return false;
    }
    // Shift via negative numbers: a direct "position - 1" can hit the primary key mid-update.
    // Pass 1: p -> -p for everything after the removed row. Pass 2: -p -> p - 1.
    query.prepare(u"UPDATE playlist_tracks SET position = -position WHERE playlist_id = ? AND position > ?"_s);
    query.addBindValue(id);
    query.addBindValue(position);
    const bool negated = query.exec();
    query.prepare(u"UPDATE playlist_tracks SET position = -position - 1 WHERE playlist_id = ? AND position < 0"_s);
    query.addBindValue(id);
    if (!negated || !query.exec()) {
        logFailure(query);
        db.rollback();
        return false;
    }
    return db.commit() || logFailure(db);
}
