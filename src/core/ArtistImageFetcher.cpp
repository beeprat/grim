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
#include <QRegularExpression>
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
    , m_youTubeMusicUrl(u"https://music.youtube.com"_s)
{
    QDir().mkpath(m_directory);
}

void ArtistImageFetcher::setBaseUrl(const QUrl &baseUrl)
{
    m_baseUrl = baseUrl;
}

void ArtistImageFetcher::setYouTubeMusicUrl(const QUrl &url)
{
    m_youTubeMusicUrl = url;
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
    if (m_youTubeMusicUrl.isEmpty())
        fetchDeezer(artist, std::move(onResult));
    else
        fetchYouTubeMusic(artist, std::move(onResult));
}

QNetworkReply *ArtistImageFetcher::postYouTubeMusic(const QString &endpoint, QJsonObject body)
{
    body.insert("context"_L1, QJsonObject{{"client"_L1, QJsonObject{{"clientName"_L1, "WEB_REMIX"_L1},
                                                                     {"clientVersion"_L1, "1.20241023.01.00"_L1},
                                                                     {"hl"_L1, "en"_L1}}}});
    QUrl url = m_youTubeMusicUrl.resolved(QUrl(u"/youtubei/v1/"_s + endpoint));
    url.setQuery(u"prettyPrint=false"_s);
    QNetworkRequest request = requestFor(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/json"));
    request.setRawHeader("Origin", m_youTubeMusicUrl.toString(QUrl::RemovePath).toUtf8());
    return m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

// Search (artists only), take the exact name or else the first hit, then read the banner of
// its page. Anything missing on the way falls back to Deezer.
void ArtistImageFetcher::fetchYouTubeMusic(const QString &artist, std::function<void(const QUrl &)> onResult)
{
    QNetworkReply *reply = postYouTubeMusic(u"search"_s, {{"query"_L1, artist},
                                                          {"params"_L1, "EgWKAQIgAWoKEAkQChAFEAMQBA=="_L1}});
    connect(reply, &QNetworkReply::finished, this, [this, reply, artist, onResult = std::move(onResult)]() mutable {
        reply->deleteLater();
        const QJsonDocument answer = QJsonDocument::fromJson(reply->readAll());
        QString first;
        QString exact;
        // The result rows sit deep in tabs and shelves; walk the whole answer for them.
        std::function<void(const QJsonValue &)> walk = [&](const QJsonValue &value) {
            if (!exact.isEmpty())
                return;
            if (value.isArray()) {
                for (const QJsonValue &item : value.toArray())
                    walk(item);
                return;
            }
            const QJsonObject object = value.toObject();
            const QJsonObject row = object.value("musicResponsiveListItemRenderer"_L1).toObject();
            if (!row.isEmpty()) {
                const QString id = row.value("navigationEndpoint"_L1).toObject().value("browseEndpoint"_L1)
                                       .toObject().value("browseId"_L1).toString();
                const QString name = row.value("flexColumns"_L1).toArray().first().toObject()
                                         .value("musicResponsiveListItemFlexColumnRenderer"_L1).toObject()
                                         .value("text"_L1).toObject().value("runs"_L1).toArray().first()
                                         .toObject().value("text"_L1).toString();
                if (id.startsWith("UC"_L1)) {
                    if (first.isEmpty())
                        first = id;
                    if (name.compare(artist, Qt::CaseInsensitive) == 0)
                        exact = id;
                }
                return;
            }
            for (const QJsonValue &child : object)
                walk(child);
        };
        walk(answer.object());
        const QString id = exact.isEmpty() ? first : exact;
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 || id.isEmpty()) {
            fetchDeezer(artist, std::move(onResult));
            return;
        }

        QNetworkReply *page = postYouTubeMusic(u"browse"_s, {{"browseId"_L1, id}});
        connect(page, &QNetworkReply::finished, this, [this, page, artist, onResult = std::move(onResult)]() mutable {
            page->deleteLater();
            const QJsonObject header = QJsonDocument::fromJson(page->readAll()).object().value("header"_L1).toObject();
            // The header's kind varies (immersive banner, or a square visual header).
            QJsonArray thumbnails;
            for (const QJsonValue &kind : header)
                thumbnails = kind.toObject().value("thumbnail"_L1).toObject().value("musicThumbnailRenderer"_L1)
                                 .toObject().value("thumbnail"_L1).toObject().value("thumbnails"_L1).toArray();
            QString picture = thumbnails.isEmpty() ? QString() : thumbnails.last().toObject().value("url"_L1).toString();
            if (picture.isEmpty()) {
                fetchDeezer(artist, std::move(onResult));
                return;
            }
            // Ask for twice the listed size ("=w1080-h450-..." gets "=w2160-h900-...").
            static const QRegularExpression size(u"=w(\\d+)-h(\\d+)"_s);
            const QRegularExpressionMatch match = size.match(picture);
            if (match.hasMatch()) {
                picture.replace(match.capturedStart(), match.capturedLength(),
                                u"=w%1-h%2"_s.arg(match.captured(1).toInt() * 2).arg(match.captured(2).toInt() * 2));
            }
            download(artist, QUrl(picture), [this, artist, onResult = std::move(onResult)](const QUrl &file) mutable {
                if (file.isEmpty())
                    fetchDeezer(artist, std::move(onResult));
                else
                    onResult(file);
            });
        });
    });
}

void ArtistImageFetcher::fetchDeezer(const QString &artist, std::function<void(const QUrl &)> onResult)
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
        QString picture = best.value("picture_xl"_L1).toString();
        if (picture.isEmpty())
            picture = best.value("picture_big"_L1).toString();
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
