#include "providers/LibraryModel.h"

#include "providers/CoverImageProvider.h"

#include <QUrl>

#include <algorithm>

namespace {

bool lessThan(const Track &a, const Track &b)
{
    if (const int c = a.artist.compare(b.artist, Qt::CaseInsensitive); c != 0)
        return c < 0;
    if (const int c = a.album.compare(b.album, Qt::CaseInsensitive); c != 0)
        return c < 0;
    return albumOrderLess(a, b);
}

} // namespace

LibraryModel::LibraryModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(this, &QAbstractItemModel::modelReset, this, &LibraryModel::countChanged);
}

int LibraryModel::count() const
{
    return static_cast<int>(m_rows.size());
}

int LibraryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant LibraryModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};

    const Track &track = m_tracks.at(m_rows.at(index.row()));
    switch (role) {
    case TitleRole:
        return track.title;
    case ArtistRole:
        return track.artist;
    case AlbumRole:
        return track.album;
    case DurationMsRole:
        return track.durationMs;
    case UrlRole:
        return QUrl::fromLocalFile(track.path);
    case CoverUrlRole:
        return CoverImageProvider::urlFor(track.path);
    case YearRole:
        return track.year;
    case GenreRole:
        return track.genre;
    default:
        return {};
    }
}

QHash<int, QByteArray> LibraryModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {AlbumRole, "album"},
        {DurationMsRole, "durationMs"},
        {UrlRole, "url"},
        {CoverUrlRole, "coverUrl"},
        {YearRole, "year"},
        {GenreRole, "genre"},
    };
}

const QList<Track> &LibraryModel::tracks() const
{
    return m_tracks;
}

QList<QUrl> LibraryModel::urls() const
{
    QList<QUrl> urls;
    urls.reserve(m_rows.size());
    for (const int row : m_rows)
        urls.append(QUrl::fromLocalFile(m_tracks.at(row).path));
    return urls;
}

void LibraryModel::setTracks(QList<Track> tracks)
{
    std::sort(tracks.begin(), tracks.end(), lessThan);
    m_tracks = std::move(tracks);
    indexPaths();
    applyFilter();
    emit tracksChanged();
}

void LibraryModel::addTrack(const Track &track)
{
    const auto existing = std::find_if(m_tracks.begin(), m_tracks.end(),
                                       [&](const Track &t) { return t.path.compare(track.path, Qt::CaseInsensitive) == 0; });
    if (existing != m_tracks.end())
        *existing = track;
    else
        m_tracks.insert(std::upper_bound(m_tracks.begin(), m_tracks.end(), track, lessThan), track);
    // Imports are rare; a reset keeps the row mapping simple.
    indexPaths();
    applyFilter();
    emit tracksChanged();
}

void LibraryModel::removeTracks(const QStringList &paths)
{
    const qsizetype removed = m_tracks.removeIf([&](const Track &track) {
        return paths.contains(track.path, Qt::CaseInsensitive);
    });
    if (removed == 0)
        return;
    indexPaths();
    applyFilter();
    emit tracksChanged();
}

void LibraryModel::setFilter(const QString &text)
{
    const QString filter = text.trimmed();
    if (filter == m_filter)
        return;
    m_filter = filter;
    applyFilter();
}

const Track *LibraryModel::findTrack(const QString &path) const
{
    const auto found = m_byPath.constFind(path.toLower());
    return found == m_byPath.cend() ? nullptr : &m_tracks.at(*found);
}

AlbumsModel::SortOrder LibraryModel::sortOrder() const
{
    return m_sortOrder;
}

void LibraryModel::setSortOrder(AlbumsModel::SortOrder order)
{
    if (order == m_sortOrder)
        return;
    m_sortOrder = order;
    applyFilter();
    emit sortOrderChanged();
}

void LibraryModel::setRatingSource(std::function<int(const QString &)> rating)
{
    m_rating = std::move(rating);
}

void LibraryModel::ratingsChanged()
{
    if (m_sortOrder == AlbumsModel::ByRating)
        applyFilter();
}

void LibraryModel::indexPaths()
{
    m_byPath.clear();
    for (int i = 0; i < m_tracks.size(); ++i)
        m_byPath.insert(m_tracks.at(i).path.toLower(), i);
}

void LibraryModel::applyFilter()
{
    beginResetModel();
    m_rows.clear();
    for (int i = 0; i < m_tracks.size(); ++i) {
        const Track &track = m_tracks.at(i);
        if (m_filter.isEmpty() || track.title.contains(m_filter, Qt::CaseInsensitive)
            || track.artist.contains(m_filter, Qt::CaseInsensitive)
            || track.album.contains(m_filter, Qt::CaseInsensitive))
            m_rows.append(i);
    }
    // m_tracks is in artist order; every other order falls back to it (lower index first).
    const auto key = [&](int i) { return m_rating ? m_rating(m_tracks.at(i).path) : 0; };
    std::stable_sort(m_rows.begin(), m_rows.end(), [&](int ia, int ib) {
        const Track &a = m_tracks.at(ia);
        const Track &b = m_tracks.at(ib);
        switch (m_sortOrder) {
        case AlbumsModel::ByTitle:
            if (const int c = a.title.compare(b.title, Qt::CaseInsensitive); c != 0)
                return c < 0;
            break;
        case AlbumsModel::ByYear:
            if (a.year != b.year)
                return a.year != 0 && (b.year == 0 || a.year < b.year); // unknown year last
            break;
        case AlbumsModel::ByRating:
            if (const int ra = key(ia), rb = key(ib); ra != rb)
                return ra > rb; // unrated (0) ends up last
            break;
        case AlbumsModel::ByGenre:
            if (a.genre.isEmpty() != b.genre.isEmpty())
                return b.genre.isEmpty();
            if (const int c = a.genre.compare(b.genre, Qt::CaseInsensitive); c != 0)
                return c < 0;
            break;
        case AlbumsModel::ByArtist:
            break;
        }
        return ia < ib;
    });
    endResetModel();
}
