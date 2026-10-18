#pragma once

#include "models/Track.h"

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

// Looks up lyrics on LRCLIB (lrclib.net: free, no account), preferring synced (LRC) ones,
// and caches the answer on disk, including "nothing found" (retried after a week).
// Sends only artist, title, album and length to the service.
class LyricsFetcher : public QObject
{
    Q_OBJECT

public:
    explicit LyricsFetcher(const QString &cacheDirectory, QObject *parent = nullptr);

    // For tests; defaults to https://lrclib.net.
    void setBaseUrl(const QUrl &baseUrl);

    // From the disk cache only. *known is false when the track was never looked up
    // (or a "not found" answer expired), so a fetch is worth it.
    QString cached(const Track &track, bool *known) const;

    // Calls onResult exactly once: LRC or plain text, or empty when there are none.
    // Network failures are not cached, so the next play tries again.
    void fetch(const Track &track, std::function<void(const QString &lyrics)> onResult);

private:
    QString cacheBase(const Track &track) const;
    void store(const Track &track, const QString &lyrics);
    void search(const Track &track, std::function<void(const QString &)> onResult);

    QNetworkAccessManager *m_network;
    QString m_directory;
    QUrl m_baseUrl;
};
