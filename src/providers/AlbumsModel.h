#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

class LibraryModel;

// Albums built from all library tracks: one row per album name + artist.
// Tracks without an album tag are grouped by their folder instead.
// Follows LibraryModel and rebuilds whenever it changes. The search filter
// keeps albums whose title, artist or any track title matches.
class AlbumsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use LibraryController.albums")
    Q_PROPERTY(SortOrder sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged FINAL)
    // Rows shown (after filters).
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    // Genres of the albums matching the search and artist filters, A–Z with unknown last:
    // [{key, name, count}]. `key` goes to setGenreFilter(); `name` is empty for "no genre tag".
    Q_PROPERTY(QVariantList genres READ genres NOTIFY genresChanged FINAL)

public:
    enum SortOrder {
        ByArtist, // artist, then album title
        ByTitle,  // album title A–Z
        ByYear,   // oldest first; albums without a year last
        ByRating, // highest average star rating first; unrated last
        ByGenre,  // genre A–Z, then artist; albums without a genre last
    };
    Q_ENUM(SortOrder)

    enum Role {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        TrackCountRole,
        CoverUrlRole,
        YearRole, // 0 if no track has a year tag
        GenreRole,
        RatingRole, // average of the album's rated tracks (0 = none rated), e.g. 3.5
    };

    // `rating` gives a track's stars (0 = unrated) by path, for the rating sort.
    AlbumsModel(LibraryModel &tracks, std::function<int(const QString &)> rating, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // For the album page: {title, artist, year, trackCount, coverUrl}.
    Q_INVOKABLE QVariantMap albumInfo(int row) const;
    // Row of the album containing this file (local path or file:// URL), -1 if not shown.
    Q_INVOKABLE int rowForTrack(const QUrl &fileUrl) const;
    // For the album page: [{title, artist, durationMs, url}] in track order.
    Q_INVOKABLE QVariantList tracksOf(int row) const;
    // For PlayerController.playQueue().
    Q_INVOKABLE QList<QUrl> urlsOf(int row) const;
    // Every track of every album currently shown (after search/artist filters), album by album.
    Q_INVOKABLE QList<QUrl> allUrls() const;

    void setFilter(const QString &text);
    // Only albums of this genre (a `key` from genres()); empty shows all.
    void setGenreFilter(const QString &key);
    QVariantList genres() const;
    // Only this artist's albums (exact name, case-insensitive); empty shows all.
    void setArtistFilter(const QString &artist);

    int count() const;
    SortOrder sortOrder() const;
    void setSortOrder(SortOrder order);
    // Re-reads ratings (they changed); re-sorts if sorted by rating.
    void ratingsChanged();

signals:
    void sortOrderChanged();
    void countChanged();
    void genresChanged();

private:
    struct Album
    {
        QString title;
        QString artist;
        QList<int> trackRows; // indices into LibraryModel::tracks(), already in track order
        int year = 0;
        QString genre;
        double rating = 0;
    };

    // Regroups every library track into m_all (library or ratings changed), then applyFilter().
    void rebuild();
    // Filters and sorts m_all into the shown rows; cheap enough to run per search keystroke.
    void applyFilter();

    LibraryModel &m_tracks;
    std::function<int(const QString &)> m_rating;
    QList<Album> m_all;    // every album
    QList<Album> m_albums; // shown rows
    QString m_filter;
    QString m_artistFilter;
    QString m_genreFilter;
    QVariantList m_genres;
    SortOrder m_sortOrder = ByArtist;
};
