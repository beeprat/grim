#pragma once

#include "core/SlskdClient.h"

#include <QAbstractListModel>
#include <QList>
#include <QtQml/qqmlregistration.h>

// slskd search results, best sources first, with live filters for QML.
class SearchResultsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString extensionFilter READ extensionFilter WRITE setExtensionFilter NOTIFY filterChanged FINAL)
    Q_PROPERTY(int minBitRate READ minBitRate WRITE setMinBitRate NOTIFY filterChanged FINAL)
    Q_PROPERTY(int bitDepth READ bitDepth WRITE setBitDepth NOTIFY filterChanged FINAL)
    Q_PROPERTY(int sampleRate READ sampleRate WRITE setSampleRate NOTIFY filterChanged FINAL)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged FINAL)

public:
    enum Role {
        FileNameRole = Qt::UserRole + 1,
        FolderRole,
        UsernameRole,
        ExtensionRole,
        BitRateRole,
        BitDepthRole,
        SampleRateRole,
        LengthSecRole,
        SizeRole,
        HasFreeSlotRole,
        QueueLengthRole,
        UploadSpeedRole,
        QualityRole, // Slskd::qualityText()
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setFiles(QList<SlskdSearchFile> files);
    // nullptr if row is out of range.
    const SlskdSearchFile *fileAt(int row) const;
    // Files passing the filter, in display order.
    QList<SlskdSearchFile> visibleFiles() const;
    int totalCount() const;

    QString extensionFilter() const;
    void setExtensionFilter(const QString &extension);
    int minBitRate() const;
    void setMinBitRate(int minBitRate);
    int bitDepth() const;
    void setBitDepth(int bitDepth);
    int sampleRate() const;
    void setSampleRate(int sampleRate);

signals:
    void filterChanged();
    void totalCountChanged();

private:
    void applyFilter();

    QList<SlskdSearchFile> m_files; // all results, sorted
    QList<int> m_visible;           // indices into m_files that pass the filter
    SlskdSearchFilter m_filter;
};
