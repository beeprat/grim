#pragma once

#include "core/SlskdClient.h"
#include "core/SpotifyMatch.h"
#include "providers/PlaylistDownloadsModel.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class LibraryController;
class PlaylistController;
class QJSEngine;
class QNetworkAccessManager;
class QQmlEngine;
class QTimer;
class SlskdController;

// Spotify playlist import for the Playlists page: the user pastes a playlist or album link, the
// public embed page gives its name and songs (up to 100; no account or API key needed), each song
// is matched to a library file (Spotify::Matcher) and the matches become a grim playlist.
// Importing the same link again replaces that playlist (spotify/imported/<id> = grim playlist id).
//
// downloadMissing() then fetches the songs not in the library from Soulseek: it starts grim.'s slskd
// if needed, searches kParallelSearches songs at a time, queues the best result (TrackPicker), tries
// the next result when a transfer fails, and once nothing is left running rebuilds the playlist in
// Spotify's order with the new files. One instance, created in main.cpp before the QML engine.
class SpotifyController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged FINAL)
    // After an import: {name, playlistId, matched, total, missing: [{title, artist}]}.
    Q_PROPERTY(QVariantMap lastImport READ lastImport NOTIFY lastImportChanged FINAL)
    Q_PROPERTY(PlaylistDownloadsModel *downloads READ downloads CONSTANT FINAL)
    // downloadMissing() is running (something not finished yet).
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadingChanged FINAL)
    // Songs downloaded and added so far, of the missing ones.
    Q_PROPERTY(int downloadedCount READ downloadedCount NOTIFY downloadingChanged FINAL)

public:
    static constexpr int kParallelSearches = 3;
    static constexpr int kAlbumSearchMin = 3;

    SpotifyController(LibraryController &library, PlaylistController &playlists, SlskdController &slskd,
                      QObject *parent = nullptr);
    ~SpotifyController() override;

    static SpotifyController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    bool busy() const;
    QString errorText() const;
    QVariantMap lastImport() const;
    PlaylistDownloadsModel *downloads() const;
    bool downloading() const;
    int downloadedCount() const;

    Q_INVOKABLE void importLink(const QString &link);
    Q_INVOKABLE void clearResult();
    // Fetches the last import's missing songs. `searchFormat`: file type to prefer ("flac", "mp3",
    // "" = any); `saveFormat`: what they become ("original" or an AudioConverter format).
    Q_INVOKABLE void downloadMissing(const QString &searchFormat, const QString &saveFormat);
    // One song again: a fresh search (not found / failed) or the next search result (done, but the
    // wrong file).
    Q_INVOKABLE void retry(int row);
    Q_INVOKABLE void stopDownloads();

signals:
    void busyChanged();
    void errorTextChanged();
    void lastImportChanged();
    void downloadingChanged();

private:
    struct Item
    {
        int order = -1; // position in the Spotify list
        Spotify::RemoteTrack track;
        QList<SlskdSearchFile> candidates; // best first
        int tried = 0;                     // candidates queued so far
        QString username;                  // the queued file
        QString filename;
        QString downloadId;
        QString path; // in the library, once done
        qint64 deadline = 0; // search timeout (ms since epoch)
    };

    void startWhenOnline();
    void pump();
    // Song by song: the TrackPicker::searchTexts() in turn until one finds results.
    void search(int row, int attempt = 0);
    // One search per album with kAlbumSearchMin+ missing songs; songs it does not find are then
    // searched one by one.
    void searchAlbums();
    void searchAlbum(const QList<int> &rows, const QStringList &texts, int attempt);
    void queueNext(int row);
    void onTransfers(const QList<SlskdDownload> &downloads);
    void onImported(const QString &downloadId, const QString &path);
    void onImportFailed(const QString &downloadId, const QString &reason);
    void checkFinished();
    // The import's playlist, in Spotify's order, with every file found or downloaded so far.
    void rebuildPlaylist();
    int activeSearches() const;

    void setBusy(bool busy);
    void setErrorText(const QString &text);

    static inline SpotifyController *s_instance = nullptr;
    LibraryController &m_library;
    PlaylistController &m_playlists;
    SlskdController &m_slskd;
    QNetworkAccessManager *m_network;
    bool m_busy = false;
    QString m_errorText;
    QVariantMap m_lastImport;

    // The last import, for downloadMissing().
    QString m_spotifyId;
    QString m_name;
    QList<Spotify::RemoteTrack> m_tracks;
    QStringList m_paths; // per Spotify track: library file, empty if missing

    PlaylistDownloadsModel *m_downloads;
    QList<Item> m_items; // rows of m_downloads
    QString m_searchFormat;
    bool m_running = false;
    bool m_waitingForSlskd = false;
    QTimer *m_watchdog; // search timeouts
};
