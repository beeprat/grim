#pragma once

#include "models/Track.h"
#include "providers/AlbumsModel.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <functional>

// Library tracks for the list and grid views. tracks() is always all of them, sorted by
// artist, album, file. Rows are the tracks matching the search filter, in sortOrder
// (the same orders as the album grid, applied per track).
class LibraryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(AlbumsModel::SortOrder sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged FINAL)
    // Rows shown (after the filter).
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        AlbumRole,
        DurationMsRole,
        UrlRole,      // file:// URL for PlayerController.open()
        CoverUrlRole, // image://cover/... served by CoverImageProvider
        YearRole,     // 0 = unknown
        GenreRole,
    };

    explicit LibraryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Every library track, ignoring the filter.
    const QList<Track> &tracks() const;
    // The shown (filtered) tracks' file URLs in row order, for PlayerController.playQueue().
    Q_INVOKABLE QList<QUrl> urls() const;

    void setTracks(QList<Track> tracks);
    // Inserts in sort order, or replaces the entry with the same path.
    void addTrack(const Track &track);
    // Drops the tracks with these paths (case-insensitive).
    void removeTracks(const QStringList &paths);

    // Case-insensitive match on title, artist or album; empty shows everything.
    Q_INVOKABLE void setFilter(const QString &text);

    // nullptr if no track has this path (case-insensitive, as Windows paths are).
    const Track *findTrack(const QString &path) const;

    int count() const;
    AlbumsModel::SortOrder sortOrder() const;
    void setSortOrder(AlbumsModel::SortOrder order);
    // A track's stars (0 = unrated) by path, for the rating sort.
    void setRatingSource(std::function<int(const QString &)> rating);
    // Re-sorts if sorted by rating.
    void ratingsChanged();

signals:
    void sortOrderChanged();
    void countChanged();
    // The track list itself changed (scan or import), not just the filter or sort.
    void tracksChanged();

private:
    void applyFilter();
    void indexPaths();

    QList<Track> m_tracks;
    QList<int> m_rows; // indices into m_tracks that match the filter
    QHash<QString, int> m_byPath; // lower-case path -> index into m_tracks
    QString m_filter;
    AlbumsModel::SortOrder m_sortOrder = AlbumsModel::ByArtist;
    std::function<int(const QString &)> m_rating;
};
