#include "providers/AlbumsModel.h"

#include "providers/CoverImageProvider.h"
#include "providers/LibraryModel.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Genre filter key for albums without a genre tag (no real genre is a control character).
const QString kUnknownGenre = u"\x01"_s;

} // namespace

AlbumsModel::AlbumsModel(LibraryModel &tracks, std::function<int(const QString &)> rating, QObject *parent)
    : QAbstractListModel(parent)
    , m_tracks(tracks)
    , m_rating(std::move(rating))
{
    // Regroup only when the track list itself changes, not on every search keystroke
    // (the track model resets for those too).
    connect(&m_tracks, &LibraryModel::tracksChanged, this, &AlbumsModel::rebuild);
    connect(this, &QAbstractItemModel::modelReset, this, &AlbumsModel::countChanged);
    rebuild();
}

void AlbumsModel::rebuild()
{
    QList<Album> albums;
    QHash<QString, qsizetype> byKey;
    const QList<Track> &tracks = m_tracks.tracks();
    for (int row = 0; row < tracks.size(); ++row) {
        const Track &track = tracks.at(row);
        const bool tagged = !track.album.isEmpty();
        const QString title = tagged ? track.album : QFileInfo(track.path).dir().dirName();
        const QString key = (tagged ? title + u'\x1f' + track.artist : QFileInfo(track.path).absolutePath()).toLower();

        const auto found = byKey.constFind(key);
        if (found == byKey.cend()) {
            byKey.insert(key, albums.size());
            albums.append({title, track.artist, {row}, track.year, track.genre});
        } else {
            Album &album = albums[*found];
            album.trackRows.append(row);
            if (album.year == 0)
                album.year = track.year;
            if (album.genre.isEmpty())
                album.genre = track.genre;
        }
    }

    for (Album &album : albums) {
        // Track order inside an album comes from the tags, never from the album sort.
        std::stable_sort(album.trackRows.begin(), album.trackRows.end(),
                         [&](int a, int b) { return albumOrderLess(tracks.at(a), tracks.at(b)); });
        int sum = 0;
        int rated = 0;
        for (const int row : album.trackRows) {
            if (const int stars = m_rating(tracks.at(row).path); stars > 0) {
                sum += stars;
                ++rated;
            }
        }
        album.rating = rated > 0 ? double(sum) / rated : 0;
    }

    m_all = std::move(albums);
    applyFilter();
}

void AlbumsModel::applyFilter()
{
    const QList<Track> &tracks = m_tracks.tracks();
    QList<Album> albums = m_all; // implicitly shared strings and lists: a cheap copy
    if (!m_artistFilter.isEmpty()) {
        albums.removeIf([&](const Album &album) {
            return album.artist.compare(m_artistFilter, Qt::CaseInsensitive) != 0;
        });
    }
    if (!m_filter.isEmpty()) {
        albums.removeIf([&](const Album &album) {
            if (album.title.contains(m_filter, Qt::CaseInsensitive) || album.artist.contains(m_filter, Qt::CaseInsensitive))
                return false;
            return std::none_of(album.trackRows.cbegin(), album.trackRows.cend(), [&](int row) {
                return tracks.at(row).title.contains(m_filter, Qt::CaseInsensitive);
            });
        });
    }

    // The genre list reflects the other filters, not the genre filter itself.
    QMap<QString, QPair<QString, int>> byGenre; // lower-case key -> (name as tagged, album count)
    for (const Album &album : albums) {
        auto &entry = byGenre[album.genre.toLower()];
        if (entry.first.isEmpty())
            entry.first = album.genre;
        ++entry.second;
    }
    QVariantList genres;
    for (auto it = byGenre.cbegin(); it != byGenre.cend(); ++it) {
        if (!it.key().isEmpty())
            genres.append(QVariantMap{{u"key"_s, it.key()}, {u"name"_s, it->first}, {u"count"_s, it->second}});
    }
    if (const auto unknown = byGenre.constFind(QString()); unknown != byGenre.cend())
        genres.append(QVariantMap{{u"key"_s, kUnknownGenre}, {u"name"_s, QString()}, {u"count"_s, unknown->second}});
    if (genres != m_genres) {
        m_genres = genres;
        emit genresChanged();
    }
    if (!m_genreFilter.isEmpty()) {
        const QString wanted = m_genreFilter == kUnknownGenre ? QString() : m_genreFilter;
        albums.removeIf([&](const Album &album) { return album.genre.toLower() != wanted; });
    }
    const auto byArtist = [](const Album &a, const Album &b) {
        if (const int c = a.artist.compare(b.artist, Qt::CaseInsensitive); c != 0)
            return c < 0;
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    };
    // Each order falls back to artist/title so equal keys stay in a stable, readable order.
    std::stable_sort(albums.begin(), albums.end(), [&](const Album &a, const Album &b) {
        switch (m_sortOrder) {
        case ByTitle:
            if (const int c = a.title.compare(b.title, Qt::CaseInsensitive); c != 0)
                return c < 0;
            break;
        case ByYear:
            if (a.year != b.year)
                return a.year != 0 && (b.year == 0 || a.year < b.year); // unknown year last
            break;
        case ByRating:
            if (a.rating != b.rating)
                return a.rating > b.rating; // unrated (0) ends up last
            break;
        case ByGenre:
            if (a.genre.isEmpty() != b.genre.isEmpty())
                return b.genre.isEmpty();
            if (const int c = a.genre.compare(b.genre, Qt::CaseInsensitive); c != 0)
                return c < 0;
            break;
        case ByArtist:
            break;
        }
        return byArtist(a, b);
    });

    beginResetModel();
    m_albums = std::move(albums);
    endResetModel();
}

int AlbumsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_albums.size());
}

QVariant AlbumsModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};

    const Album &album = m_albums.at(index.row());
    switch (role) {
    case TitleRole:
        return album.title;
    case ArtistRole:
        return album.artist;
    case TrackCountRole:
        return static_cast<int>(album.trackRows.size());
    case CoverUrlRole:
        return CoverImageProvider::urlFor(m_tracks.tracks().at(album.trackRows.first()).path);
    case YearRole:
        return album.year;
    case GenreRole:
        return album.genre;
    case RatingRole:
        return album.rating;
    default:
        return {};
    }
}

QHash<int, QByteArray> AlbumsModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {TrackCountRole, "trackCount"},
        {CoverUrlRole, "coverUrl"},
        {YearRole, "year"},
        {GenreRole, "genre"},
        {RatingRole, "rating"},
    };
}

int AlbumsModel::count() const
{
    return static_cast<int>(m_albums.size());
}

AlbumsModel::SortOrder AlbumsModel::sortOrder() const
{
    return m_sortOrder;
}

void AlbumsModel::setSortOrder(SortOrder order)
{
    if (order == m_sortOrder)
        return;
    m_sortOrder = order;
    applyFilter();
    emit sortOrderChanged();
}

void AlbumsModel::ratingsChanged()
{
    // Only the rating sort and the rating role depend on it; skip the rebuild otherwise,
    // so rating a track on the album page does not reset the album grid.
    if (m_sortOrder == ByRating)
        rebuild();
}

void AlbumsModel::setArtistFilter(const QString &artist)
{
    if (artist == m_artistFilter)
        return;
    m_artistFilter = artist;
    applyFilter();
}

void AlbumsModel::setGenreFilter(const QString &key)
{
    if (key == m_genreFilter)
        return;
    m_genreFilter = key;
    applyFilter();
}

QVariantList AlbumsModel::genres() const
{
    return m_genres;
}

void AlbumsModel::setFilter(const QString &text)
{
    const QString filter = text.trimmed();
    if (filter == m_filter)
        return;
    m_filter = filter;
    applyFilter();
}

QVariantMap AlbumsModel::albumInfo(int row) const
{
    if (row < 0 || row >= m_albums.size())
        return {};
    const QModelIndex i = index(row);
    return {
        {u"title"_s, data(i, TitleRole)},
        {u"artist"_s, data(i, ArtistRole)},
        {u"year"_s, data(i, YearRole)},
        {u"trackCount"_s, data(i, TrackCountRole)},
        {u"coverUrl"_s, data(i, CoverUrlRole)},
    };
}

int AlbumsModel::rowForTrack(const QUrl &fileUrl) const
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    for (int row = 0; row < m_albums.size(); ++row) {
        for (const int trackRow : m_albums.at(row).trackRows) {
            if (m_tracks.tracks().at(trackRow).path.compare(path, Qt::CaseInsensitive) == 0)
                return row;
        }
    }
    return -1;
}

QVariantList AlbumsModel::tracksOf(int row) const
{
    QVariantList result;
    if (row < 0 || row >= m_albums.size())
        return result;
    for (const int trackRow : m_albums.at(row).trackRows) {
        const Track &track = m_tracks.tracks().at(trackRow);
        result.append(QVariantMap{
            {u"title"_s, track.title},
            {u"artist"_s, track.artist},
            {u"durationMs"_s, track.durationMs},
            {u"url"_s, QUrl::fromLocalFile(track.path)},
        });
    }
    return result;
}

QList<QUrl> AlbumsModel::allUrls() const
{
    QList<QUrl> urls;
    for (int row = 0; row < m_albums.size(); ++row)
        urls.append(urlsOf(row));
    return urls;
}

QList<QUrl> AlbumsModel::urlsOf(int row) const
{
    QList<QUrl> urls;
    if (row < 0 || row >= m_albums.size())
        return urls;
    for (const int trackRow : m_albums.at(row).trackRows)
        urls.append(QUrl::fromLocalFile(m_tracks.tracks().at(trackRow).path));
    return urls;
}
