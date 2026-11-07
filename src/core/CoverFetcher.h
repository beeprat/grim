#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

// Finds an album's front cover online (Deezer's album search, api.deezer.com; sends only the artist and
// album name). Answers are remembered for the session, and lookups for the same album share one request,
// so a batch of downloads from one album asks once.
class CoverFetcher : public QObject
{
    Q_OBJECT

public:
    explicit CoverFetcher(QObject *parent = nullptr);

    // For tests; defaults to https://api.deezer.com.
    void setBaseUrl(const QUrl &baseUrl);

    // Calls onCover exactly once: JPEG bytes, or empty when nothing matching was found (or offline).
    void fetch(const QString &artist, const QString &album, std::function<void(const QByteArray &)> onCover);

private:
    void finish(const QString &key, const QByteArray &cover, bool remember);

    QNetworkAccessManager *m_network;
    QUrl m_baseUrl;
    QHash<QString, QByteArray> m_found; // artist|album key -> cover, empty = none
    QHash<QString, QList<std::function<void(const QByteArray &)>>> m_pending;
};
