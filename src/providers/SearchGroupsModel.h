#pragma once

#include "core/SlskdClient.h"

#include <QAbstractListModel>
#include <QList>
#include <QtQml/qqmlregistration.h>

class SearchResultsModel;

// Soulseek results grouped for display: by album (one user's folder) or by
// artist (that artist's albums across users). Built from the filtered results,
// so "albums in FLAC" is the Albums view with the FLAC filter.
class SearchGroupsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use SlskdController.groups")
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged FINAL)

public:
    enum Mode {
        Albums,
        Artists,
    };
    Q_ENUM(Mode)

    enum Role {
        TitleRole = Qt::UserRole + 1,
        SubtitleRole,
        FormatsRole,  // e.g. "FLAC · MP3"
        QualityRole,  // of the album's first file; empty in Artists mode
        FileCountRole,
        SizeRole,
        HasFreeSlotRole,
        QueueLengthRole,
        AlbumIndexRole, // Albums mode: index for SlskdController.downloadAlbum()
        ItemsRole,      // Albums: its tracks; Artists: its albums (each with albumIndex)
    };

    explicit SearchGroupsModel(SearchResultsModel &results, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Mode mode() const;
    void setMode(Mode mode);

    // nullptr if out of range.
    const Slskd::Album *albumAt(int albumIndex) const;

signals:
    void modeChanged();

private:
    struct Artist
    {
        QString name;
        QList<int> albumIndices;
    };

    void rebuild();
    QVariantMap albumSummary(int albumIndex) const;

    SearchResultsModel &m_results;
    Mode m_mode = Albums;
    QList<Slskd::Album> m_albums;
    QList<Artist> m_artists;
};
