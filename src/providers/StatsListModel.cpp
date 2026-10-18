#include "providers/StatsListModel.h"

#include <algorithm>

int StatsListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant StatsListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};

    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case TitleRole:
        return entry.title;
    case SubtitleRole:
        return entry.subtitle;
    case PlayCountRole:
        return entry.playCount;
    case ListenedMsRole:
        return entry.listenedMs;
    case UrlRole:
        return entry.url;
    case ShareRole:
        return m_maxListenedMs > 0 ? double(entry.listenedMs) / double(m_maxListenedMs) : 0.0;
    default:
        return {};
    }
}

QHash<int, QByteArray> StatsListModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {SubtitleRole, "subtitle"},
        {PlayCountRole, "playCount"},
        {ListenedMsRole, "listenedMs"},
        {UrlRole, "url"},
        {ShareRole, "share"},
    };
}

void StatsListModel::setEntries(QList<Entry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    const auto byListened = [](const Entry &a, const Entry &b) { return a.listenedMs < b.listenedMs; };
    const auto maxIt = std::max_element(m_entries.cbegin(), m_entries.cend(), byListened);
    m_maxListenedMs = maxIt != m_entries.cend() ? maxIt->listenedMs : 0;
    endResetModel();
}
