#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

// Read-only ranked list (top tracks, artists or albums) for the statistics page.
class StatsListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS

public:
    struct Entry
    {
        QString title;
        QString subtitle;
        qint64 playCount = 0;
        qint64 listenedMs = 0;
        QUrl url; // tracks: the track; albums: one of its tracks; artists: empty
    };

    enum Role {
        TitleRole = Qt::UserRole + 1,
        SubtitleRole,
        PlayCountRole,
        ListenedMsRole,
        UrlRole,
        ShareRole, // listenedMs / highest listenedMs in the list, 0..1
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(QList<Entry> entries);

private:
    QList<Entry> m_entries;
    qint64 m_maxListenedMs = 0;
};
