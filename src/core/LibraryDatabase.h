#pragma once

#include <QHash>
#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

struct Playlist
{
    qint64 id = 0;
    QString name;
    int trackCount = 0;
};

// The user's own data about tracks: star ratings, favorites and playlists,
// in library.sqlite, keyed by absolute file path (forward slashes).
// Ratings and favorites are cached in memory, so lookups are cheap enough for
// every visible row. Not thread-safe: use it only from the thread that opened it.
class LibraryDatabase
{
public:
    LibraryDatabase();
    ~LibraryDatabase();
    LibraryDatabase(const LibraryDatabase &) = delete;
    LibraryDatabase &operator=(const LibraryDatabase &) = delete;

    // Opens or creates the file and migrates the schema. Call once.
    bool open(const QString &filePath);
    bool isOpen() const;

    int rating(const QString &path) const; // 0 = not rated, else 1..5
    bool setRating(const QString &path, int stars);
    bool isFavorite(const QString &path) const;
    bool setFavorite(const QString &path, bool favorite);
    QStringList favorites() const; // most recently favorited first

    QList<Playlist> playlists() const; // by name
    qint64 createPlaylist(const QString &name, const QStringList &paths); // 0 on failure
    bool renamePlaylist(qint64 id, const QString &name);
    bool deletePlaylist(qint64 id);
    QStringList playlistTracks(qint64 id) const; // in playlist order
    bool addToPlaylist(qint64 id, const QStringList &paths);
    bool removeFromPlaylist(qint64 id, int position);

private:
    struct Meta
    {
        int rating = 0;
        bool favorite = false;
    };

    QSqlDatabase database() const;
    bool migrate();
    bool saveMeta(const QString &path, const Meta &meta);

    QString m_connectionName;
    QHash<QString, Meta> m_meta;
};
