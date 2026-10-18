#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

// Uploads images to imgur anonymously (Client-ID only, no account) and remembers the
// link per image content (sha1) in `cacheFile`, so each unique cover goes up once.
class ImgurUploader : public QObject
{
    Q_OBJECT

public:
    explicit ImgurUploader(const QString &cacheFile, QObject *parent = nullptr);

    // For tests; defaults to https://api.imgur.com/3/image.
    void setEndpoint(const QUrl &endpoint);
    void setClientId(const QString &clientId);

    static QString keyOf(const QByteArray &image);
    // Link from an earlier upload of the same bytes, or empty.
    QString cachedUrl(const QString &key) const;

    // onDone runs exactly once: the https link, or empty on failure (not cached, so the
    // next call tries again). Calls for an image already uploading wait for that upload.
    void upload(const QByteArray &image, std::function<void(const QString &url)> onDone);

signals:
    void errorOccurred(const QString &message);

private:
    void save() const;

    QNetworkAccessManager *m_network;
    QString m_cacheFile;
    QUrl m_endpoint;
    QString m_clientId;
    // key -> {link, deletehash}; the deletehash lets the user remove an upload later.
    QHash<QString, QPair<QString, QString>> m_links;
    QHash<QString, QList<std::function<void(const QString &)>>> m_pending;
};
