#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QJsonObject;
class QNetworkAccessManager;
class QNetworkReply;

// Finds an artist photo and keeps it on disk, including "no photo" (retried after a week): the
// wide header banner from YouTube Music (its web client's API, no account), else Deezer's photo
// (api.deezer.com). Sends only the artist name to the services.
class ArtistImageFetcher : public QObject
{
    Q_OBJECT

public:
    explicit ArtistImageFetcher(const QString &cacheDirectory, QObject *parent = nullptr);

    // For tests; defaults to https://api.deezer.com.
    void setBaseUrl(const QUrl &baseUrl);
    // For tests; defaults to https://music.youtube.com. Empty skips YouTube Music.
    void setYouTubeMusicUrl(const QUrl &url);

    // The cached photo as a file URL, or empty. *known is false when the artist was never
    // looked up (or a "no photo" answer expired), so a fetch is worth it.
    QUrl cached(const QString &artist, bool *known) const;

    // Calls onResult exactly once: the cached photo's file URL, or empty when there is none.
    // Network failures are not cached, so the next call tries again.
    void fetch(const QString &artist, std::function<void(const QUrl &file)> onResult);

private:
    QString cacheBase(const QString &artist) const;
    void fetchYouTubeMusic(const QString &artist, std::function<void(const QUrl &)> onResult);
    void fetchDeezer(const QString &artist, std::function<void(const QUrl &)> onResult);
    QNetworkReply *postYouTubeMusic(const QString &endpoint, QJsonObject body);
    void download(const QString &artist, const QUrl &picture, std::function<void(const QUrl &)> onResult);

    QNetworkAccessManager *m_network;
    QString m_directory;
    QUrl m_baseUrl;
    QUrl m_youTubeMusicUrl;
};
