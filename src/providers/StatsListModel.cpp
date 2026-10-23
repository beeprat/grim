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
    const auto byListened = [](const Entry &a, const Entry &b) { return a.listenedMs < b.listenedMs; };
    const auto maxIt = std::max_element(entries.cbegin(), entries.cend(), byListened);
    const qint64 maxListenedMs = maxIt != entries.cend() ? maxIt->listenedMs : 0;

    // Refreshed on every visit and every few seconds: when the same rows come back in the same order
    // (only the numbers moved), update them in place. A reset rebuilds every row and reloads its cover,
    // which made opening the page lag.
    const auto sameRow = [](const Entry &a, const Entry &b) {
        return a.title == b.title && a.subtitle == b.subtitle && a.url == b.url;
    };
    if (!m_entries.isEmpty()
        && std::equal(entries.cbegin(), entries.cend(), m_entries.cbegin(), m_entries.cend(), sameRow)) {
        m_entries = std::move(entries);
        m_maxListenedMs = maxListenedMs;
        emit dataChanged(index(0), index(int(m_entries.size()) - 1), {PlayCountRole, ListenedMsRole, ShareRole});
        return;
    }

    beginResetModel();
    m_entries = std::move(entries);
    m_maxListenedMs = maxListenedMs;
    endResetModel();
}
