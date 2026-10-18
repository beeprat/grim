#include "providers/DownloadsModel.h"

#include <algorithm>

int DownloadsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_downloads.size());
}

QVariant DownloadsModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};

    const SlskdDownload &download = m_downloads.at(index.row());
    switch (role) {
    case FileNameRole:
        return download.filename.section(u'\\', -1);
    case UsernameRole:
        return download.username;
    case StateRole:
        return download.state;
    case ProgressRole:
        return download.size > 0 ? double(download.bytesTransferred) / double(download.size) : 0.0;
    case FinishedRole:
        return Slskd::isTransferFinished(download.state);
    case NoteRole:
        return m_notes.value(download.id);
    default:
        return {};
    }
}

QHash<int, QByteArray> DownloadsModel::roleNames() const
{
    return {
        {FileNameRole, "fileName"},
        {UsernameRole, "username"},
        {StateRole, "state"},
        {ProgressRole, "progress"},
        {FinishedRole, "finished"},
        {NoteRole, "note"},
    };
}

void DownloadsModel::setDownloads(QList<SlskdDownload> downloads)
{
    const bool sameRows = std::equal(downloads.cbegin(), downloads.cend(), m_downloads.cbegin(), m_downloads.cend(),
                                     [](const SlskdDownload &a, const SlskdDownload &b) { return a.id == b.id; });
    if (!sameRows) {
        beginResetModel();
        m_downloads = std::move(downloads);
        endResetModel();
        return;
    }

    m_downloads = std::move(downloads);
    if (!m_downloads.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {StateRole, ProgressRole, FinishedRole});
}

void DownloadsModel::setNote(const QString &downloadId, const QString &note)
{
    m_notes.insert(downloadId, note);
    for (int row = 0; row < m_downloads.size(); ++row) {
        if (m_downloads.at(row).id == downloadId) {
            emit dataChanged(index(row), index(row), {NoteRole});
            return;
        }
    }
}

bool DownloadsModel::hasActive() const
{
    return std::any_of(m_downloads.cbegin(), m_downloads.cend(),
                       [](const SlskdDownload &d) { return !Slskd::isTransferFinished(d.state); });
}
