#include "providers/AlbumsModel.h"

#include "providers/CoverImageProvider.h"
#include "providers/LibraryModel.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMap>

#include <algorithm>
#include <limits>

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
            albums.append({title, track.artist, {row}, track.year, track.genre, 0, std::max(track.addedMs, track.changedMs)});
        } else {
            Album &album = albums[*found];
            album.trackRows.append(row);
            if (album.year == 0)
                album.year = track.year;
            if (album.genre.isEmpty())
                album.genre = track.genre;
            album.addedMs = std::max({album.addedMs, track.addedMs, track.changedMs}); // new songs or retags bring it back up
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
    struct GenreEntry
    {
        QString name; // as tagged
        int count = 0;
        QStringList covers; // up to 4, for the genre tile
    };
    QMap<QString, GenreEntry> byGenre; // lower-case key
    for (const Album &album : albums) {
        GenreEntry &entry = byGenre[album.genre.toLower()];
        if (entry.name.isEmpty())
            entry.name = album.genre;
        ++entry.count;
        if (entry.covers.size() < 4)
            entry.covers.append(CoverImageProvider::urlFor(tracks.at(album.trackRows.first()).path));
    }
    const auto genreMap = [](const QString &key, const GenreEntry &entry) {
        return QVariantMap{{u"key"_s, key}, {u"name"_s, entry.name}, {u"count"_s, entry.count}, {u"covers"_s, entry.covers}};
    };
    QVariantList genres;
    for (auto it = byGenre.cbegin(); it != byGenre.cend(); ++it) {
        if (!it.key().isEmpty())
            genres.append(genreMap(it.key(), *it));
    }
    if (const auto unknown = byGenre.constFind(QString()); unknown != byGenre.cend())
        genres.append(genreMap(kUnknownGenre, *unknown));
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
        case ByAdded:
            if (a.addedMs != b.addedMs)
                return a.addedMs > b.addedMs;
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

QVariantList AlbumsModel::albumsBy(const QString &artist, const QString &exceptTitle) const
{
    if (artist.isEmpty())
        return {};
    QList<const Album *> found;
    for (const Album &album : m_all) {
        if (album.artist.compare(artist, Qt::CaseInsensitive) == 0
            && album.title.compare(exceptTitle, Qt::CaseInsensitive) != 0)
            found.append(&album);
    }
    std::stable_sort(found.begin(), found.end(), [](const Album *a, const Album *b) { return a->year > b->year; });
    return toList(found);
}

QVariantList AlbumsModel::toList(const QList<const Album *> &albums) const
{
    const QList<Track> &tracks = m_tracks.tracks();
    QVariantList result;
    for (const Album *album : albums) {
        const QString path = tracks.at(album->trackRows.first()).path;
        result.append(QVariantMap{{u"title"_s, album->title},
                                  {u"artist"_s, album->artist},
                                  {u"year"_s, album->year},
                                  {u"coverUrl"_s, CoverImageProvider::urlFor(path)},
                                  {u"url"_s, QUrl::fromLocalFile(path)}});
    }
    return result;
}

QVariantList AlbumsModel::recentlyAdded(int limit) const
{
    QList<const Album *> found;
    for (const Album &album : m_all)
        found.append(&album);
    std::stable_sort(found.begin(), found.end(), [](const Album *a, const Album *b) { return a->addedMs > b->addedMs; });
    return toList(found.mid(0, limit));
}

QVariantList AlbumsModel::topRated(int limit) const
{
    QList<const Album *> found;
    for (const Album &album : m_all) {
        if (album.rating > 0)
            found.append(&album);
    }
    std::stable_sort(found.begin(), found.end(), [](const Album *a, const Album *b) { return a->rating > b->rating; });
    return toList(found.mid(0, limit));
}
