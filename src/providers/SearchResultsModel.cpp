#include "providers/SearchResultsModel.h"

#include <algorithm>

int SearchResultsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_visible.size());
}

const SlskdSearchFile *SearchResultsModel::fileAt(int row) const
{
    if (row < 0 || row >= m_visible.size())
        return nullptr;
    return &m_files.at(m_visible.at(row));
}

QList<SlskdSearchFile> SearchResultsModel::visibleFiles() const
{
    QList<SlskdSearchFile> files;
    files.reserve(m_visible.size());
    for (const int index : m_visible)
        files.append(m_files.at(index));
    return files;
}

QVariant SearchResultsModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};

    const SlskdSearchFile &file = m_files.at(m_visible.at(index.row()));
    switch (role) {
    case FileNameRole:
        return file.filename.section(u'\\', -1);
    case FolderRole:
        return file.filename.section(u'\\', 0, -2);
    case UsernameRole:
        return file.username;
    case ExtensionRole:
        return file.extension;
    case BitRateRole:
        return file.bitRate;
    case BitDepthRole:
        return file.bitDepth;
    case SampleRateRole:
        return file.sampleRate;
    case LengthSecRole:
        return file.lengthSec;
    case SizeRole:
        return file.size;
    case HasFreeSlotRole:
        return file.hasFreeSlot;
    case QueueLengthRole:
        return file.queueLength;
    case UploadSpeedRole:
        return file.uploadSpeed;
    case QualityRole:
        return Slskd::qualityText(file);
    default:
        return {};
    }
}

QHash<int, QByteArray> SearchResultsModel::roleNames() const
{
    return {
        {FileNameRole, "fileName"},
        {FolderRole, "folder"},
        {UsernameRole, "username"},
        {ExtensionRole, "extension"},
        {BitRateRole, "bitRate"},
        {BitDepthRole, "bitDepth"},
        {SampleRateRole, "sampleRate"},
        {LengthSecRole, "lengthSec"},
        {SizeRole, "size"},
        {HasFreeSlotRole, "hasFreeSlot"},
        {QueueLengthRole, "queueLength"},
        {UploadSpeedRole, "uploadSpeed"},
        {QualityRole, "quality"},
    };
}

void SearchResultsModel::setFiles(QList<SlskdSearchFile> files)
{
    // Free slot first (download starts now), then fastest uploader, then shortest queue.
    std::stable_sort(files.begin(), files.end(), [](const SlskdSearchFile &a, const SlskdSearchFile &b) {
        if (a.hasFreeSlot != b.hasFreeSlot)
            return a.hasFreeSlot;
        if (a.uploadSpeed != b.uploadSpeed)
            return a.uploadSpeed > b.uploadSpeed;
        return a.queueLength < b.queueLength;
    });

    const bool totalChanged = files.size() != m_files.size();
    m_files = std::move(files);
    applyFilter();
    if (totalChanged)
        emit totalCountChanged();
}

int SearchResultsModel::totalCount() const
{
    return static_cast<int>(m_files.size());
}

void SearchResultsModel::applyFilter()
{
    beginResetModel();
    m_visible.clear();
    for (int i = 0; i < m_files.size(); ++i) {
        if (Slskd::matches(m_files.at(i), m_filter))
            m_visible.append(i);
    }
    endResetModel();
}

QString SearchResultsModel::extensionFilter() const
{
    return m_filter.extension;
}

void SearchResultsModel::setExtensionFilter(const QString &extension)
{
    if (extension == m_filter.extension)
        return;
    m_filter.extension = extension;
    applyFilter();
    emit filterChanged();
}

int SearchResultsModel::minBitRate() const
{
    return m_filter.minBitRate;
}

void SearchResultsModel::setMinBitRate(int minBitRate)
{
    if (minBitRate == m_filter.minBitRate)
        return;
    m_filter.minBitRate = minBitRate;
    applyFilter();
    emit filterChanged();
}

int SearchResultsModel::bitDepth() const
{
    return m_filter.bitDepth;
}

void SearchResultsModel::setBitDepth(int bitDepth)
{
    if (bitDepth == m_filter.bitDepth)
        return;
    m_filter.bitDepth = bitDepth;
    applyFilter();
    emit filterChanged();
}

int SearchResultsModel::sampleRate() const
{
    return m_filter.sampleRate;
}

void SearchResultsModel::setSampleRate(int sampleRate)
{
    if (sampleRate == m_filter.sampleRate)
        return;
    m_filter.sampleRate = sampleRate;
    applyFilter();
    emit filterChanged();
}
