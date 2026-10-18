#include "providers/SearchGroupsModel.h"

#include "providers/SearchResultsModel.h"

#include <QHash>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

QString formatsOf(const QList<SlskdSearchFile> &files)
{
    QStringList formats;
    for (const SlskdSearchFile &file : files) {
        const QString format = file.extension.toUpper();
        if (!format.isEmpty() && !formats.contains(format))
            formats.append(format);
    }
    return formats.join(u" · "_s);
}

} // namespace

SearchGroupsModel::SearchGroupsModel(SearchResultsModel &results, QObject *parent)
    : QAbstractListModel(parent)
    , m_results(results)
{
    // The results model resets on every new response batch and filter change.
    connect(&m_results, &QAbstractItemModel::modelReset, this, &SearchGroupsModel::rebuild);
    rebuild();
}

void SearchGroupsModel::rebuild()
{
    beginResetModel();
    m_albums = Slskd::groupByAlbum(m_results.visibleFiles());

    m_artists.clear();
    QHash<QString, qsizetype> byName;
    for (int i = 0; i < m_albums.size(); ++i) {
        const QString key = m_albums.at(i).artist.toLower();
        auto found = byName.constFind(key);
        if (found == byName.cend()) {
            found = byName.insert(key, m_artists.size());
            m_artists.append({m_albums.at(i).artist, {}});
        }
        m_artists[*found].albumIndices.append(i);
    }
    // Most albums first; unknown artist last.
    std::stable_sort(m_artists.begin(), m_artists.end(), [](const Artist &a, const Artist &b) {
        if (a.name.isEmpty() != b.name.isEmpty())
            return b.name.isEmpty();
        return a.albumIndices.size() > b.albumIndices.size();
    });
    endResetModel();
}

int SearchGroupsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_mode == Albums ? m_albums.size() : m_artists.size());
}

const Slskd::Album *SearchGroupsModel::albumAt(int albumIndex) const
{
    return albumIndex >= 0 && albumIndex < m_albums.size() ? &m_albums.at(albumIndex) : nullptr;
}

QVariantMap SearchGroupsModel::albumSummary(int albumIndex) const
{
    const Slskd::Album &album = m_albums.at(albumIndex);
    const SlskdSearchFile &first = album.files.first();
    return {
        {u"albumIndex"_s, albumIndex},
        {u"title"_s, album.title},
        {u"username"_s, album.username},
        {u"fileCount"_s, static_cast<int>(album.files.size())},
        {u"size"_s, album.totalSize},
        {u"formats"_s, formatsOf(album.files)},
        {u"quality"_s, Slskd::qualityText(first)},
        {u"hasFreeSlot"_s, first.hasFreeSlot},
        {u"queueLength"_s, first.queueLength},
    };
}

QVariant SearchGroupsModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};

    if (m_mode == Albums) {
        const Slskd::Album &album = m_albums.at(index.row());
        const SlskdSearchFile &first = album.files.first();
        switch (role) {
        case TitleRole:
            return album.title;
        case SubtitleRole:
            return album.artist.isEmpty() ? album.username : album.artist + u"  ·  "_s + album.username;
        case FormatsRole:
            return formatsOf(album.files);
        case QualityRole:
            return Slskd::qualityText(first);
        case FileCountRole:
            return static_cast<int>(album.files.size());
        case SizeRole:
            return album.totalSize;
        case HasFreeSlotRole:
            return first.hasFreeSlot;
        case QueueLengthRole:
            return first.queueLength;
        case AlbumIndexRole:
            return index.row();
        case ItemsRole: {
            QVariantList tracks;
            for (const SlskdSearchFile &file : album.files) {
                tracks.append(QVariantMap{
                    {u"fileName"_s, file.filename.section(u'\\', -1)},
                    {u"size"_s, file.size},
                    {u"lengthSec"_s, file.lengthSec},
                    {u"quality"_s, Slskd::qualityText(file)},
                });
            }
            return tracks;
        }
        default:
            return {};
        }
    }

    const Artist &artist = m_artists.at(index.row());
    switch (role) {
    case TitleRole:
        return artist.name.isEmpty() ? tr("Unknown artist") : artist.name;
    case SubtitleRole: {
        QSet<QString> users;
        for (const int i : artist.albumIndices)
            users.insert(m_albums.at(i).username);
        return tr("%1 albums from %2 users").arg(artist.albumIndices.size()).arg(users.size());
    }
    case FormatsRole: {
        QList<SlskdSearchFile> files;
        for (const int i : artist.albumIndices)
            files.append(m_albums.at(i).files);
        return formatsOf(files);
    }
    case FileCountRole: {
        int count = 0;
        for (const int i : artist.albumIndices)
            count += static_cast<int>(m_albums.at(i).files.size());
        return count;
    }
    case SizeRole: {
        qint64 size = 0;
        for (const int i : artist.albumIndices)
            size += m_albums.at(i).totalSize;
        return size;
    }
    case HasFreeSlotRole:
        return std::any_of(artist.albumIndices.cbegin(), artist.albumIndices.cend(),
                           [this](int i) { return m_albums.at(i).files.first().hasFreeSlot; });
    case QueueLengthRole:
        return 0;
    case QualityRole:
        return QString();
    case AlbumIndexRole:
        return -1;
    case ItemsRole: {
        QVariantList albums;
        for (const int i : artist.albumIndices)
            albums.append(albumSummary(i));
        return albums;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> SearchGroupsModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {SubtitleRole, "subtitle"},
        {FormatsRole, "formats"},
        {QualityRole, "quality"},
        {FileCountRole, "fileCount"},
        {SizeRole, "size"},
        {HasFreeSlotRole, "hasFreeSlot"},
        {QueueLengthRole, "queueLength"},
        {AlbumIndexRole, "albumIndex"},
        {ItemsRole, "items"},
    };
}

SearchGroupsModel::Mode SearchGroupsModel::mode() const
{
    return m_mode;
}

void SearchGroupsModel::setMode(Mode mode)
{
    if (mode == m_mode)
        return;
    beginResetModel();
    m_mode = mode;
    endResetModel();
    emit modeChanged();
}
