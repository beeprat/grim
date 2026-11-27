#pragma once

#include <QList>

#include <algorithm>

namespace QueueOrder {

// Drops play positions `from`.. out of `order` (indices into `queue`, in play order) and their entries
// out of `queue`; the kept indices move down past the removed ones.
template<typename T>
void dropFrom(QList<int> &order, QList<T> &queue, int from)
{
    if (from < 0 || from >= order.size())
        return;
    QList<int> removed = order.mid(from);
    order.resize(from);
    std::sort(removed.begin(), removed.end());
    for (auto it = removed.crbegin(); it != removed.crend(); ++it)
        queue.removeAt(*it);
    // Each kept index moves down by the number of removed indices below it.
    for (int &index : order)
        index -= int(std::lower_bound(removed.cbegin(), removed.cend(), index) - removed.cbegin());
}

} // namespace QueueOrder
