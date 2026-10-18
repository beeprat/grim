#pragma once

#include "providers/AlbumsModel.h"
#include "core/LibraryIndex.h"
#include "providers/LibraryModel.h"

#include <QFutureWatcher>
#include <QObject>
#include <QtQml/qqmlregistration.h>

class LibraryDatabase;
class QJSEngine;
class QQmlEngine;
class SettingsController;

class QTimer;

// The music library: every playable file under the library folder.
// Scans run on a worker thread; one instance, created in main.cpp.
class LibraryController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(LibraryModel *tracks READ tracks CONSTANT FINAL)
    Q_PROPERTY(AlbumsModel *albums READ albums CONSTANT FINAL)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged FINAL)
    // Search text for the library page; filters both tracks and albums.
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged FINAL)
    // Albums of one artist only (from Statistics → Top artists); empty shows every album.
    Q_PROPERTY(QString artistFilter READ artistFilter WRITE setArtistFilter NOTIFY artistFilterChanged FINAL)
    // Albums of one genre only (a key from albums.genres, picked in the genre list); empty = all.
    Q_PROPERTY(QString genreFilter READ genreFilter WRITE setGenreFilter NOTIFY genreFilterChanged FINAL)
    // All tracks with their own search, for the new-playlist track picker.
    Q_PROPERTY(LibraryModel *pickerTracks READ pickerTracks CONSTANT FINAL)
    // Bumped on every rating/favorite change. Bindings that call rating() or isFavorite()
    // read it too, so they re-evaluate: `metaRevision >= 0 ? rating(url) : 0` (not a comma
    // expression: compiled QML drops the dependency of an unused comma operand).
    Q_PROPERTY(int metaRevision READ metaRevision NOTIFY metaChanged FINAL)

public:
    LibraryController(SettingsController &settings, LibraryDatabase &database, QObject *parent = nullptr);
    ~LibraryController() override;

    static LibraryController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    LibraryModel *tracks() const;
    AlbumsModel *albums() const;
    bool scanning() const;
    QString filter() const;
    void setFilter(const QString &text);
    QString artistFilter() const;
    void setArtistFilter(const QString &artist);
    QString genreFilter() const;
    void setGenreFilter(const QString &key);
    LibraryModel *pickerTracks() const;
    int metaRevision() const;

    // Reads every file's tags again (the refresh button; the library folder changed).
    Q_INVOKABLE void rescan();

    // Ratings (0 = none, 1..5) and favorites for a file:// URL.
    Q_INVOKABLE int rating(const QUrl &fileUrl) const;
    Q_INVOKABLE void setRating(const QUrl &fileUrl, int stars);
    Q_INVOKABLE bool isFavorite(const QUrl &fileUrl) const;
    Q_INVOKABLE void toggleFavorite(const QUrl &fileUrl);

    // Opens Explorer with the file selected.
    Q_INVOKABLE void showInFolder(const QUrl &fileUrl) const;
    // Sends these files to the Recycle Bin, with their folder once it holds no more music,
    // and drops them from the library. Empty on success, else a message for the user.
    Q_INVOKABLE QString deleteAlbum(const QList<QUrl> &fileUrls);

    // nullptr if the file is not in the library.
    const Track *findTrack(const QString &path) const;

    // Adds one new file (e.g. a converted download) without a full rescan.
    void addFile(const QString &path);

signals:
    void scanningChanged();
    void filterChanged();
    void artistFilterChanged();
    void genreFilterChanged();
    void metaChanged();
    // Emitted before files are deleted, so the player can let go of them (Windows refuses to
    // delete open files).
    void aboutToDelete(const QStringList &paths);

private:
    static inline LibraryController *s_instance = nullptr;

    SettingsController &m_settings;
    LibraryDatabase &m_database;
    LibraryModel *m_tracks;
    LibraryModel *m_pickerTracks;
    int m_metaRevision = 0;
    AlbumsModel *m_albums;
    // Reads the folder; `full` ignores the index and reads every file's tags again.
    void startScan(bool full);

    QFutureWatcher<LibraryIndex::ScanResult> m_scan;
    QString m_indexFile;
    QString m_filter;
    QTimer *m_filterDelay;
    QString m_artistFilter;
    QString m_genreFilter;
    bool m_rescanPending = false; // folder changed mid-scan: drop that result and scan again
};
