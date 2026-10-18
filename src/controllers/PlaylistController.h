#pragma once

#include <QList>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class LibraryController;
class LibraryDatabase;
class QJSEngine;
class QQmlEngine;

// Playlists page backend. Id 0 is the built-in "Favorites" list (hearted tracks);
// real playlists have ids > 0. One instance, created in main.cpp.
class PlaylistController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // [{id, name, trackCount}], Favorites first, then playlists by name.
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY playlistsChanged FINAL)
    Q_PROPERTY(qint64 currentId READ currentId WRITE setCurrentId NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged FINAL)
    // [{title, artist, album, durationMs, url, coverUrl}] of the current playlist, in order.
    Q_PROPERTY(QVariantList currentTracks READ currentTracks NOTIFY currentChanged FINAL)

public:
    static constexpr qint64 kFavoritesId = 0;

    PlaylistController(LibraryDatabase &database, LibraryController &library, QObject *parent = nullptr);
    ~PlaylistController() override;

    static PlaylistController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    QVariantList playlists() const;
    qint64 currentId() const;
    void setCurrentId(qint64 id);
    QString currentName() const;
    QVariantList currentTracks() const;

    // Returns the new playlist's id (0 if the name is empty) and selects it.
    Q_INVOKABLE qint64 createPlaylist(const QString &name, const QList<QUrl> &fileUrls);
    Q_INVOKABLE void renamePlaylist(qint64 id, const QString &name);
    Q_INVOKABLE void deletePlaylist(qint64 id);
    Q_INVOKABLE void addTracks(qint64 id, const QList<QUrl> &fileUrls);
    // Removes one entry of the current playlist (for Favorites: un-hearts it).
    Q_INVOKABLE void removeTrack(int position);
    // The current playlist's files, for PlayerController.playQueue().
    Q_INVOKABLE QList<QUrl> currentUrls() const;

signals:
    void playlistsChanged();
    void currentChanged();

private:
    QStringList currentPaths() const;
    void refresh();

    static inline PlaylistController *s_instance = nullptr;

    LibraryDatabase &m_database;
    LibraryController &m_library;
    qint64 m_currentId = kFavoritesId;
};
