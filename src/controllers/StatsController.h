#pragma once

#include "core/StatsDatabase.h"
#include "providers/StatsListModel.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <functional>
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
    // Ignores rangeDays: milestones count all listening ever.
    Q_PROPERTY(qint64 allTimeListenedMs READ allTimeListenedMs NOTIFY totalsChanged FINAL)
    Q_PROPERTY(StatsListModel *topTracks READ topTracks CONSTANT FINAL)
    Q_PROPERTY(StatsListModel *topArtists READ topArtists CONSTANT FINAL)
    Q_PROPERTY(StatsListModel *topAlbums READ topAlbums CONSTANT FINAL)
    // The number one of each list for the highlight cards: {title, subtitle, listenedMs, playCount,
    // url, image}; empty maps when nothing was played. The artist's image is a photo from Deezer.
    Q_PROPERTY(QVariantMap topTrack READ topTrack NOTIFY highlightsChanged FINAL)
    Q_PROPERTY(QVariantMap topArtist READ topArtist NOTIFY highlightsChanged FINAL)
    Q_PROPERTY(QVariantMap topAlbum READ topAlbum NOTIFY highlightsChanged FINAL)
    // Days the totals, lists, highlights and charts cover, counting today: 7, 30, 90; 0 is all time.
    Q_PROPERTY(int rangeDays READ rangeDays WRITE setRangeDays NOTIFY rangeDaysChanged FINAL)
    // Time listened per genre, most first, small ones merged into "Other": [{genre, key, ms}]; key is the
    // library's genre filter key, empty for "Unknown" and "Other" (nothing to open).
    Q_PROPERTY(QVariantList genreShares READ genreShares NOTIFY chartsChanged FINAL)
    // Time listened per day over the range, oldest first, quiet days included: [{date, ms}].
    Q_PROPERTY(QVariantList dailyListening READ dailyListening NOTIFY chartsChanged FINAL)

public:
    explicit StatsController(StatsDatabase &database, QObject *parent = nullptr);
    ~StatsController() override;

    static StatsController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    qint64 totalPlays() const;
    qint64 totalListenedMs() const;
    qint64 allTimeListenedMs() const;
    StatsListModel *topTracks() const;
    StatsListModel *topArtists() const;
    StatsListModel *topAlbums() const;
    QVariantMap topTrack() const;
    QVariantMap topArtist() const;
    QVariantMap topAlbum() const;
    int rangeDays() const;
    void setRangeDays(int days);
    QVariantList genreShares() const;
    QVariantList dailyListening() const;
    // How a played file's genre is found (the library knows it, the stats database does not).
    void setGenreLookup(std::function<QString(const QString &path)> lookup);

    // {playCount, listenedMs} of one artist.
    Q_INVOKABLE QVariantMap artistStats(const QString &artist) const;
    // The artist's cached photo, or empty; a missing one is fetched and artistImageReady follows.
    Q_INVOKABLE QUrl artistImage(const QString &artist);

    // Re-reads everything from the database.
    Q_INVOKABLE void refresh();
    // Home page rows: albums last played, or most listened. [{title, artist, coverUrl, url}]
    Q_INVOKABLE QVariantList homeAlbums(bool recent, int limit) const;
    // The first `limit` of `albums` (entries with title and artist) never listened to.
    Q_INVOKABLE QVariantList neverPlayed(const QVariantList &albums, int limit) const;
    // Writes the listening history to a JSON file, paths also relative to the music folder so it
    // imports on any system. Empty on success, else a message for the user.
    Q_INVOKABLE QString exportTo(const QUrl &fileUrl, const QString &libraryFolder);
    // Adds the plays of an exported file (ones already here are skipped). A message for the user.
    Q_INVOKABLE QString importFrom(const QUrl &fileUrl, const QString &libraryFolder);

signals:
    void totalsChanged();
    void highlightsChanged();
    void rangeDaysChanged();
    void chartsChanged();
    void artistImageReady(const QString &artist, const QUrl &image);

private:
    static inline StatsController *s_instance = nullptr;

    StatsDatabase &m_database;
    StatsTotals m_totals;
    qint64 m_allTimeListenedMs = 0;
    StatsListModel *m_topTracks;
    StatsListModel *m_topArtists;
    StatsListModel *m_topAlbums;
    QVariantMap m_topTrack;
    QVariantMap m_topArtist;
    QVariantMap m_topAlbum;
    ArtistImageFetcher *m_artistImages;
    QString m_imageArtist; // whose photo m_artistImage is (or is being fetched)
    QUrl m_artistImage;
    int m_rangeDays = 0;
    QVariantList m_genreShares;
    QVariantList m_dailyListening;
    std::function<QString(const QString &)> m_genreOf;
};
