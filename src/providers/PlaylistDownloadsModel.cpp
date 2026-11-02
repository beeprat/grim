#include "providers/PlaylistDownloadsModel.h"

PlaylistDownloadsModel::PlaylistDownloadsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int PlaylistDownloadsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant PlaylistDownloadsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case TitleRole:
        return row.title;
    case ArtistRole:
        return row.artist;
    case StateRole:
        return int(row.state);
    case DetailRole:
        return row.detail;
    case RetryableRole:
        return row.state == NotFound || row.state == Failed || row.state == Done;
    default:
        return {};
    }
}

QHash<int, QByteArray> PlaylistDownloadsModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {StateRole, "stage"},
        {DetailRole, "detail"},
        {RetryableRole, "retryable"},
    };
}

void PlaylistDownloadsModel::setRows(QList<Row> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

void PlaylistDownloadsModel::update(int row, State state, const QString &detail)
{
    if (row < 0 || row >= m_rows.size())
        return;
    Row &r = m_rows[row];
    if (r.state == state && r.detail == detail)
        return;
    r.state = state;
    r.detail = detail;
    emit dataChanged(index(row), index(row));
}

const PlaylistDownloadsModel::Row &PlaylistDownloadsModel::at(int row) const
{
    return m_rows.at(row);
}

int PlaylistDownloadsModel::size() const
{
    return int(m_rows.size());
}
