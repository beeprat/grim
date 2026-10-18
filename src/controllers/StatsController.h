#pragma once

#include "core/StatsDatabase.h"
#include "providers/StatsListModel.h"

#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class ArtistImageFetcher;
class QJSEngine;
class QQmlEngine;

// Exposes listening statistics to QML. One instance, created in main.cpp
// before the QML engine and handed to QML through create().
class StatsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(qint64 totalPlays READ totalPlays NOTIFY totalsChanged FINAL)
    Q_PROPERTY(qint64 totalListenedMs READ totalListenedMs NOTIFY totalsChanged FINAL)
    Q_PROPERTY(StatsListModel *topTracks READ topTracks CONSTANT FINAL)
    Q_PROPERTY(StatsListModel *topArtists READ topArtists CONSTANT FINAL)
    Q_PROPERTY(StatsListModel *topAlbums READ topAlbums CONSTANT FINAL)
    // The number one of each list for the highlight cards: {title, subtitle, listenedMs, playCount,
    // url, image}; empty maps when nothing was played. The artist's image is a photo from Deezer.
    Q_PROPERTY(QVariantMap topTrack READ topTrack NOTIFY highlightsChanged FINAL)
    Q_PROPERTY(QVariantMap topArtist READ topArtist NOTIFY highlightsChanged FINAL)
    Q_PROPERTY(QVariantMap topAlbum READ topAlbum NOTIFY highlightsChanged FINAL)

public:
    explicit StatsController(StatsDatabase &database, QObject *parent = nullptr);
    ~StatsController() override;

    static StatsController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    qint64 totalPlays() const;
    qint64 totalListenedMs() const;
    StatsListModel *topTracks() const;
    StatsListModel *topArtists() const;
    StatsListModel *topAlbums() const;
    QVariantMap topTrack() const;
    QVariantMap topArtist() const;
    QVariantMap topAlbum() const;

    // Re-reads everything from the database.
    Q_INVOKABLE void refresh();
    // Writes the listening history to a JSON file. Empty on success, else a message for the user.
    Q_INVOKABLE QString exportTo(const QUrl &fileUrl);
    // Adds the plays of an exported file (ones already here are skipped). A message for the user.
    Q_INVOKABLE QString importFrom(const QUrl &fileUrl);

signals:
    void totalsChanged();
    void highlightsChanged();

private:
    static inline StatsController *s_instance = nullptr;

    StatsDatabase &m_database;
    StatsTotals m_totals;
    StatsListModel *m_topTracks;
    StatsListModel *m_topArtists;
    StatsListModel *m_topAlbums;
    QVariantMap m_topTrack;
    QVariantMap m_topArtist;
    QVariantMap m_topAlbum;
    ArtistImageFetcher *m_artistImages;
    QString m_imageArtist; // whose photo m_artistImage is (or is being fetched)
    QUrl m_artistImage;
};
