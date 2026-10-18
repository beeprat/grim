#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

// Finds an artist photo on Deezer (api.deezer.com, free, no account) and keeps it on disk,
// including "no photo" (retried after a week). Sends only the artist name to the service.
class ArtistImageFetcher : public QObject
{
    Q_OBJECT

public:
    explicit ArtistImageFetcher(const QString &cacheDirectory, QObject *parent = nullptr);

    // For tests; defaults to https://api.deezer.com.
    void setBaseUrl(const QUrl &baseUrl);

    // The cached photo as a file URL, or empty. *known is false when the artist was never
    // looked up (or a "no photo" answer expired), so a fetch is worth it.
    QUrl cached(const QString &artist, bool *known) const;

    // Calls onResult exactly once: the cached photo's file URL, or empty when there is none.
    // Network failures are not cached, so the next call tries again.
    void fetch(const QString &artist, std::function<void(const QUrl &file)> onResult);

private:
    QString cacheBase(const QString &artist) const;
    void download(const QString &artist, const QUrl &picture, std::function<void(const QUrl &)> onResult);

    QNetworkAccessManager *m_network;
    QString m_directory;
    QUrl m_baseUrl;
};
