#pragma once

#include "core/SlskdClient.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QtQml/qqmlregistration.h>

// slskd download queue for QML. Updates in place when the list shape is
// unchanged, so polling does not reset the view's scroll position.
class DownloadsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Role {
        FileNameRole = Qt::UserRole + 1,
        UsernameRole,
        StateRole,
        ProgressRole, // 0..1
        FinishedRole,
        NoteRole, // conversion status, empty if none
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDownloads(QList<SlskdDownload> downloads);
    bool hasActive() const;
    SlskdDownload at(int row) const { return m_downloads.value(row); }
    void setNote(const QString &downloadId, const QString &note);

private:
    QList<SlskdDownload> m_downloads;
    QHash<QString, QString> m_notes; // by download id; survives list refreshes
};
