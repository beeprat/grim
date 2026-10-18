#include "core/ImgurUploader.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

namespace {

constexpr int kTimeoutMs = 30000;

} // namespace

ImgurUploader::ImgurUploader(const QString &cacheFile, QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_cacheFile(cacheFile)
    , m_endpoint(u"https://api.imgur.com/3/image"_s)
{
    QFile file(m_cacheFile);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject entries = QJsonDocument::fromJson(file.readAll()).object();
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            const QJsonObject entry = it.value().toObject();
            const QString link = entry.value("link"_L1).toString();
            if (link.startsWith("https://"_L1))
                m_links.insert(it.key(), {link, entry.value("deletehash"_L1).toString()});
        }
    }
}

void ImgurUploader::setEndpoint(const QUrl &endpoint)
{
    m_endpoint = endpoint;
}

void ImgurUploader::setClientId(const QString &clientId)
{
    m_clientId = clientId.trimmed();
}

QString ImgurUploader::keyOf(const QByteArray &image)
{
    return QString::fromLatin1(QCryptographicHash::hash(image, QCryptographicHash::Sha1).toHex());
}

QString ImgurUploader::cachedUrl(const QString &key) const
{
    return m_links.value(key).first;
}

void ImgurUploader::upload(const QByteArray &image, std::function<void(const QString &)> onDone)
{
    const QString key = keyOf(image);
    if (const QString link = cachedUrl(key); !link.isEmpty()) {
        onDone(link);
        return;
    }
    if (m_clientId.isEmpty() || image.isEmpty()) {
        onDone({});
        return;
    }
    const bool alreadyUploading = m_pending.contains(key);
    m_pending[key].append(std::move(onDone));
    if (alreadyUploading)
        return;

    // Base64 holds '+' and '/', which a form body must percent-encode.
    const QByteArray body = "type=base64&image=" + QUrl::toPercentEncoding(QString::fromLatin1(image.toBase64()));

    QNetworkRequest request(m_endpoint);
    request.setRawHeader("Authorization", "Client-ID " + m_clientId.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/x-www-form-urlencoded"));
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply *reply = m_network->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key] {
        reply->deleteLater();
        const QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object().value("data"_L1).toObject();
        const QString link = data.value("link"_L1).toString();
        if (link.startsWith("https://"_L1)) {
            m_links.insert(key, {link, data.value("deletehash"_L1).toString()});
            save();
        } else {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            emit errorOccurred(status == 0 ? u"Cannot reach imgur"_s
                                           : u"imgur upload failed (HTTP %1)"_s.arg(status));
        }
        for (const auto &callback : m_pending.take(key))
            callback(link.startsWith("https://"_L1) ? link : QString());
    });
}

void ImgurUploader::save() const
{
    QJsonObject entries;
    for (auto it = m_links.cbegin(); it != m_links.cend(); ++it)
        entries.insert(it.key(), QJsonObject{{"link"_L1, it.value().first}, {"deletehash"_L1, it.value().second}});
    QSaveFile file(m_cacheFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(entries).toJson());
        file.commit();
    }
}
