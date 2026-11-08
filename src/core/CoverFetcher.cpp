#include "core/CoverFetcher.h"

#include "core/DownloadTagger.h"

#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

namespace {

QNetworkRequest requestFor(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArray("GRIM/0.1 (Qt desktop music player)"));
    request.setTransferTimeout(10'000);
    return request;
}

} // namespace

CoverFetcher::CoverFetcher(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_baseUrl(u"https://api.deezer.com"_s)
{
}

void CoverFetcher::setBaseUrl(const QUrl &baseUrl)
{
    m_baseUrl = baseUrl;
}

void CoverFetcher::fetch(const QString &artist, const QString &album, std::function<void(const QByteArray &)> onCover)
{
    if (artist.trimmed().isEmpty() || album.trimmed().isEmpty()) {
        onCover({});
        return;
    }
    const QString albumKey = DownloadTagger::albumKey(album);
    const QString key = DownloadTagger::artistKey(artist) + u'|' + albumKey;
    if (const auto found = m_found.constFind(key); found != m_found.cend()) {
        onCover(*found);
        return;
    }
    auto &waiting = m_pending[key];
    waiting.append(std::move(onCover));
    if (waiting.size() > 1)
        return; // already asked
    fetchItunes(key, artist, album);
}

void CoverFetcher::fetchDeezer(const QString &key, const QString &artist, const QString &album)
{
    // Fallback when iTunes has no match.
    const QString albumKey = DownloadTagger::albumKey(album);
    QUrl url = m_baseUrl.resolved(QUrl(u"/search/album"_s));
    url.setQuery(QUrlQuery{{u"q"_s, artist.trimmed() + u' ' + album.trimmed()},
                           {u"limit"_s, u"25"_s}});
    QNetworkReply *reply = m_network->get(requestFor(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, artist, album, albumKey] {
        reply->deleteLater();
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            finish(key, {}, false); // offline: asked again next time
            return;
        }
        // The same album name (edition notes ignored) by the same artist; nothing else, so a wrong cover
        // never lands in the files.
        // Deezer answers errors (quota, outages) with 200 and an "error" object: not an answer, so not kept.
        const QJsonValue data = QJsonDocument::fromJson(reply->readAll()).object().value("data"_L1);
        if (!data.isArray()) {
            finish(key, {}, false);
            return;
        }
        QString picture;
        for (const QJsonValue &value : data.toArray()) {
            const QJsonObject candidate = value.toObject();
            if (DownloadTagger::albumKey(candidate.value("title"_L1).toString()) != albumKey
                || DownloadTagger::artistKey(candidate.value("artist"_L1).toObject().value("name"_L1).toString())
                       != DownloadTagger::artistKey(artist))
                continue;
            picture = candidate.value("cover_xl"_L1).toString();
            if (picture.isEmpty())
                picture = candidate.value("cover_big"_L1).toString();
            if (!picture.isEmpty())
                break;
        }
        if (picture.isEmpty()) {
            finish(key, {}, true);
            return;
        }
        downloadImage(key, QUrl(picture));
    });
}

void CoverFetcher::fetchItunes(const QString &key, const QString &artist, const QString &album)
{
    // First choice: Apple's iTunes Search API; same exact-name rule as Deezer.
    QUrl url(u"https://itunes.apple.com/search"_s);
    url.setQuery(QUrlQuery{{u"term"_s, artist.trimmed() + u' ' + album.trimmed()},
                           {u"entity"_s, u"album"_s},
                           {u"limit"_s, u"25"_s}});
    QNetworkReply *reply = m_network->get(requestFor(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, artist, album] {
        reply->deleteLater();
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            fetchDeezer(key, artist, album);
            return;
        }
        const QJsonValue results = QJsonDocument::fromJson(reply->readAll()).object().value("results"_L1);
        if (!results.isArray()) {
            fetchDeezer(key, artist, album);
            return;
        }
        QString picture;
        for (const QJsonValue &value : results.toArray()) {
            const QJsonObject candidate = value.toObject();
            if (DownloadTagger::albumKey(candidate.value("collectionName"_L1).toString()) != DownloadTagger::albumKey(album)
                || DownloadTagger::artistKey(candidate.value("artistName"_L1).toString()) != DownloadTagger::artistKey(artist))
                continue;
            picture = candidate.value("artworkUrl100"_L1).toString();
            if (!picture.isEmpty())
                break;
        }
        if (picture.isEmpty()) {
            fetchDeezer(key, artist, album);
            return;
        }
        picture.replace(u"100x100bb"_s, u"600x600bb"_s); // Apple scales the artwork to the size in the URL
        downloadImage(key, QUrl(picture));
    });
}

void CoverFetcher::downloadImage(const QString &key, const QUrl &picture)
{
    QNetworkReply *image = m_network->get(requestFor(picture));
    connect(image, &QNetworkReply::finished, this, [this, image, key] {
        image->deleteLater();
        const QByteArray bytes = image->readAll();
        const bool ok = image->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
                        && !QImage::fromData(bytes).isNull();
        finish(key, ok ? bytes : QByteArray(), ok);
    });
}

void CoverFetcher::finish(const QString &key, const QByteArray &cover, bool remember)
{
    if (remember)
        m_found.insert(key, cover);
    const auto waiting = m_pending.take(key);
    for (const auto &onCover : waiting)
        onCover(cover);
}
