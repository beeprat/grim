#include "controllers/PlaylistController.h"

#include "controllers/LibraryController.h"
#include "core/LibraryDatabase.h"
#include "providers/CoverImageProvider.h"

#include <QFileInfo>
#include <QJSEngine>

using namespace Qt::StringLiterals;

namespace {

QStringList toPaths(const QList<QUrl> &fileUrls)
{
    QStringList paths;
    for (const QUrl &url : fileUrls) {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    }
    return paths;
}

} // namespace

PlaylistController::PlaylistController(LibraryDatabase &database, LibraryController &library, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_library(library)
{
    Q_ASSERT(!s_instance);
    s_instance = this;

    // Hearts change the Favorites list and its count.
    connect(&m_library, &LibraryController::metaChanged, this, &PlaylistController::refresh);
    // Track titles/covers come from the library; show them once the scan is done.
    connect(&m_library, &LibraryController::scanningChanged, this, &PlaylistController::currentChanged);
}

PlaylistController::~PlaylistController()
{
    s_instance = nullptr;
}

PlaylistController *PlaylistController::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "PlaylistController::create", "construct PlaylistController before loading QML");
    // Owned by main.cpp; stop the QML engine from deleting it.
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void PlaylistController::refresh()
{
    emit playlistsChanged();
    emit currentChanged();
}

QVariantList PlaylistController::playlists() const
{
    QVariantList result{QVariantMap{
        {u"id"_s, kFavoritesId},
        {u"name"_s, tr("Favorites")},
        {u"trackCount"_s, static_cast<int>(m_database.favorites().size())},
    }};
    for (const Playlist &playlist : m_database.playlists())
        result.append(QVariantMap{{u"id"_s, playlist.id}, {u"name"_s, playlist.name}, {u"trackCount"_s, playlist.trackCount}});
    return result;
}

qint64 PlaylistController::currentId() const
{
    return m_currentId;
}

void PlaylistController::setCurrentId(qint64 id)
{
    if (id == m_currentId)
        return;
    m_currentId = id;
    emit currentChanged();
}

QString PlaylistController::currentName() const
{
    if (m_currentId == kFavoritesId)
        return tr("Favorites");
    for (const Playlist &playlist : m_database.playlists()) {
        if (playlist.id == m_currentId)
            return playlist.name;
    }
    return {};
}

QStringList PlaylistController::currentPaths() const
{
    return m_currentId == kFavoritesId ? m_database.favorites() : m_database.playlistTracks(m_currentId);
}

QVariantList PlaylistController::currentTracks() const
{
    QVariantList tracks;
    for (const QString &path : currentPaths()) {
        // Tags come from the library scan; a file outside the library shows its name.
        const Track *track = m_library.findTrack(path);
        tracks.append(QVariantMap{
            {u"title"_s, track ? track->title : QFileInfo(path).completeBaseName()},
            {u"artist"_s, track ? track->artist : QString()},
            {u"album"_s, track ? track->album : QString()},
            {u"durationMs"_s, track ? track->durationMs : 0},
            {u"url"_s, QUrl::fromLocalFile(path)},
            {u"coverUrl"_s, CoverImageProvider::urlFor(path)},
        });
    }
    return tracks;
}

QList<QUrl> PlaylistController::currentUrls() const
{
    QList<QUrl> urls;
    for (const QString &path : currentPaths())
        urls.append(QUrl::fromLocalFile(path));
    return urls;
}

qint64 PlaylistController::createPlaylist(const QString &name, const QList<QUrl> &fileUrls)
{
    const qint64 id = m_database.createPlaylist(name, toPaths(fileUrls));
    if (id > 0) {
        m_currentId = id;
        refresh();
    }
    return id;
}

void PlaylistController::renamePlaylist(qint64 id, const QString &name)
{
    if (id != kFavoritesId && m_database.renamePlaylist(id, name))
        refresh();
}

void PlaylistController::deletePlaylist(qint64 id)
{
    if (id == kFavoritesId || !m_database.deletePlaylist(id))
        return;
    if (m_currentId == id)
        m_currentId = kFavoritesId;
    refresh();
}

void PlaylistController::addTracks(qint64 id, const QList<QUrl> &fileUrls)
{
    if (id == kFavoritesId) {
        for (const QUrl &url : fileUrls) {
            if (!m_library.isFavorite(url))
                m_library.toggleFavorite(url); // emits metaChanged -> refresh
        }
        return;
    }
    if (m_database.addToPlaylist(id, toPaths(fileUrls)))
        refresh();
}

void PlaylistController::removeTrack(int position)
{
    if (m_currentId == kFavoritesId) {
        const QStringList favorites = m_database.favorites();
        if (position >= 0 && position < favorites.size())
            m_library.toggleFavorite(QUrl::fromLocalFile(favorites.at(position)));
        return;
    }
    if (m_database.removeFromPlaylist(m_currentId, position))
        refresh();
}
