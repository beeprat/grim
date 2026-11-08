#include "core/LibrarySidecar.h"

#include "core/LibraryDatabase.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

using namespace Qt::StringLiterals;

namespace {

QString root(const QString &libraryFolder)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(libraryFolder)) + u'/';
}

// Path relative to the folder, empty if outside it. The database keys use forward slashes.
QString relative(const QString &path, const QString &prefix)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    return clean.startsWith(prefix, Qt::CaseInsensitive) ? clean.mid(prefix.size()) : QString();
}

// Drops names that would leave the folder ("../") or are absolute.
QString absolute(const QString &relativePath, const QString &prefix)
{
    const QString clean = QDir::cleanPath(relativePath);
    if (clean.isEmpty() || clean.startsWith(u"../"_s) || clean == u".."_s || QDir::isAbsolutePath(clean))
        return {};
    return prefix + clean;
}

} // namespace

QJsonObject LibrarySidecar::toJson(const LibraryDatabase &data, const QString &libraryFolder)
{
    const QString prefix = root(libraryFolder);

    QJsonObject songs;
    for (const QString &path : data.metaPaths()) {
        const QString file = relative(path, prefix);
        if (file.isEmpty())
            continue;
        QJsonObject song;
        if (const int stars = data.rating(path))
            song[u"rating"_s] = stars;
        if (const std::optional<double> db = data.replayGainDb(path))
            song[u"volumeDb"_s] = *db;
        if (!song.isEmpty())
            songs[file] = song;
    }
    QJsonArray favorites; // most recent first, as LibraryDatabase::favorites()
    for (const QString &path : data.favorites())
        if (const QString file = relative(path, prefix); !file.isEmpty())
            favorites.append(file);
    QJsonArray playlists;
    for (const Playlist &playlist : data.playlists()) {
        QJsonArray tracks;
        for (const QString &path : data.playlistTracks(playlist.id))
            if (const QString file = relative(path, prefix); !file.isEmpty())
                tracks.append(file);
        playlists.append(QJsonObject{{u"name"_s, playlist.name}, {u"tracks"_s, tracks}});
    }

    return QJsonObject{{u"version"_s, 1}, {u"songs"_s, songs}, {u"favorites"_s, favorites}, {u"playlists"_s, playlists}};
}

bool LibrarySidecar::write(const LibraryDatabase &data, const QString &libraryFolder)
{
    if (libraryFolder.isEmpty() || !QDir(libraryFolder).exists())
        return false;
    QSaveFile out(root(libraryFolder) + QLatin1StringView(kFileName));
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(QJsonDocument(toJson(data, libraryFolder)).toJson());
    return out.commit();
}

bool LibrarySidecar::read(LibraryDatabase &data, const QString &libraryFolder)
{
    if (libraryFolder.isEmpty())
        return false;
    const QString prefix = root(libraryFolder);
    QFile file(prefix + QLatin1StringView(kFileName));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return merge(data, QJsonDocument::fromJson(file.readAll()).object(), libraryFolder);
}

bool LibrarySidecar::merge(LibraryDatabase &data, const QJsonObject &doc, const QString &libraryFolder)
{
    if (libraryFolder.isEmpty())
        return false;
    const QString prefix = root(libraryFolder);
    bool changed = false;

    const QJsonObject songs = doc.value(u"songs"_s).toObject();
    for (auto it = songs.begin(); it != songs.end(); ++it) {
        const QString path = absolute(it.key(), prefix);
        if (path.isEmpty())
            continue;
        const QJsonObject song = it.value().toObject();
        const int stars = song.value(u"rating"_s).toInt();
        if (stars >= 1 && stars <= 5 && data.rating(path) == 0)
            changed |= data.setRating(path, stars);
        if (song.contains(u"volumeDb"_s) && !data.replayGainDb(path))
            changed |= data.setReplayGainDb(path, song.value(u"volumeDb"_s).toDouble());
    }

    // Oldest first, so the newest ends up first in Favorites.
    // shortcut: same-second favorites sort by path, not file order; store timestamps if order matters.
    const QJsonArray favorites = doc.value(u"favorites"_s).toArray();
    for (qsizetype i = favorites.size() - 1; i >= 0; --i) {
        const QString path = absolute(favorites.at(i).toString(), prefix);
        if (!path.isEmpty() && !data.isFavorite(path))
            changed |= data.setFavorite(path, true);
    }

    QSet<QString> names;
    for (const Playlist &playlist : data.playlists())
        names.insert(playlist.name.toCaseFolded());
    for (const QJsonValue &value : doc.value(u"playlists"_s).toArray()) {
        const QJsonObject playlist = value.toObject();
        const QString name = playlist.value(u"name"_s).toString().trimmed();
        if (name.isEmpty() || names.contains(name.toCaseFolded()))
            continue;
        QStringList paths;
        for (const QJsonValue &track : playlist.value(u"tracks"_s).toArray())
            if (const QString path = absolute(track.toString(), prefix); !path.isEmpty())
                paths.append(path);
        if (data.createPlaylist(name, paths)) {
            names.insert(name.toCaseFolded());
            changed = true;
        }
    }
    return changed;
}
