#include "core/ArtistImageFetcher.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrlQuery>
#include <QtLogging>

using namespace Qt::StringLiterals;

namespace {

constexpr int kTimeoutMs = 10'000;
constexpr qint64 kNotFoundRetrySecs = 7 * 24 * 60 * 60;
constexpr auto kUserAgent = "GRIM/0.1 (Qt desktop music player)";

QNetworkRequest requestFor(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArray(kUserAgent));
    request.setTransferTimeout(kTimeoutMs);
    return request;
}

} // namespace

ArtistImageFetcher::ArtistImageFetcher(const QString &cacheDirectory, QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_directory(cacheDirectory)
    , m_baseUrl(u"https://api.deezer.com"_s)
{
    QDir().mkpath(m_directory);
}

void ArtistImageFetcher::setBaseUrl(const QUrl &baseUrl)
{
    m_baseUrl = baseUrl;
}

QString ArtistImageFetcher::cacheBase(const QString &artist) const
{
    const QByteArray hash =
        QCryptographicHash::hash(artist.trimmed().toLower().toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(m_directory).filePath(QString::fromLatin1(hash));
}

QUrl ArtistImageFetcher::cached(const QString &artist, bool *known) const
{
    const QString base = cacheBase(artist);
    if (QFileInfo::exists(base + u".jpg"_s)) {
        *known = true;
        return QUrl::fromLocalFile(base + u".jpg"_s);
    }
    const QFileInfo none(base + u".none"_s);
    *known = none.exists() && none.lastModified().secsTo(QDateTime::currentDateTime()) < kNotFoundRetrySecs;
    return {};
}

void ArtistImageFetcher::fetch(const QString &artist, std::function<void(const QUrl &)> onResult)
{
    QUrl url = m_baseUrl.resolved(QUrl(u"/search/artist"_s));
    url.setQuery(QUrlQuery{{u"q"_s, artist}, {u"limit"_s, u"10"_s}});
    QNetworkReply *reply = m_network->get(requestFor(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, artist, onResult = std::move(onResult)] {
        reply->deleteLater();
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            onResult({}); // offline or service trouble: not cached, retried next time
            return;
        }
        // The exact name if Deezer has it, else its best match. Several artists can share a name
        // (a tiny namesake often comes first, without a photo): take the one with the most fans.
        const QJsonArray results = QJsonDocument::fromJson(reply->readAll()).object().value("data"_L1).toArray();
        QJsonObject best = results.isEmpty() ? QJsonObject() : results.first().toObject();
        bool exact = false;
        for (const QJsonValue &value : results) {
            const QJsonObject candidate = value.toObject();
            if (candidate.value("name"_L1).toString().compare(artist, Qt::CaseInsensitive) != 0)
                continue;
            if (!exact || candidate.value("nb_fan"_L1).toInteger() > best.value("nb_fan"_L1).toInteger())
                best = candidate;
            exact = true;
        }
        // Artists without a photo get a generic one whose path has an empty image id ("artist//").
        const QString picture = best.value("picture_big"_L1).toString();
        if (picture.isEmpty() || picture.contains("/artist//"_L1)) {
            // An empty marker file; if it cannot be written, the lookup simply repeats next time.
            QFile none(cacheBase(artist) + u".none"_s);
            if (!none.open(QIODevice::WriteOnly | QIODevice::Truncate))
                qWarning().noquote() << "ArtistImageFetcher: cannot write" << none.fileName();
            onResult({});
            return;
        }
        download(artist, QUrl(picture), onResult);
    });
}

void ArtistImageFetcher::download(const QString &artist, const QUrl &picture,
                                  std::function<void(const QUrl &)> onResult)
{
    QNetworkReply *reply = m_network->get(requestFor(picture));
    connect(reply, &QNetworkReply::finished, this, [this, reply, artist, onResult = std::move(onResult)] {
        reply->deleteLater();
        const QByteArray bytes = reply->readAll();
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200
            || QImage::fromData(bytes).isNull()) {
            onResult({});
            return;
        }
        const QString base = cacheBase(artist);
        QSaveFile file(base + u".jpg"_s);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            onResult({});
            return;
        }
        QFile::remove(base + u".none"_s);
        onResult(QUrl::fromLocalFile(base + u".jpg"_s));
    });
}
