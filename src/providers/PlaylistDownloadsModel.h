#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

// Songs of a Spotify import being fetched from Soulseek (SpotifyController): one row per song not in
// the library, with what is happening to it.
class PlaylistDownloadsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use SpotifyController.downloads")

public:
    enum State {
        Waiting,
        Searching,
        Queued,      // asked the uploader; waiting in their queue
        Downloading,
        Adding,      // downloaded: converting / moving into the library
        Done,
        NotFound,    // no search result is this song
        Failed,      // every result tried failed, or the import did
    };
    Q_ENUM(State)

    enum Role {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        StateRole,
        DetailRole,  // file being fetched, progress or the error
        RetryableRole,
    };

    struct Row
    {
        QString title;
        QString artist;
        State state = Waiting;
        QString detail;
    };

    explicit PlaylistDownloadsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(QList<Row> rows);
    void update(int row, State state, const QString &detail = {});
    const Row &at(int row) const;
    int size() const;

private:
    QList<Row> m_rows;
};
