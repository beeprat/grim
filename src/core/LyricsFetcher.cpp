#include "core/LyricsFetcher.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int kTimeoutMs = 10'000;
constexpr qint64 kNotFoundRetrySecs = 7 * 24 * 60 * 60;
// LRCLIB asks clients to identify themselves.
constexpr auto kUserAgent = "GRIM/0.1 (Qt desktop music player)";

// Best lyrics in one LRCLIB record: synced, else plain; instrumentals have none.
QString lyricsOf(const QJsonObject &record)
{
    if (record.value("instrumental"_L1).toBool())
        return {};
    const QString synced = record.value("syncedLyrics"_L1).toString().trimmed();
    return synced.isEmpty() ? record.value("plainLyrics"_L1).toString().trimmed() : synced;
}

} // namespace

LyricsFetcher::LyricsFetcher(const QString &cacheDirectory, QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_directory(cacheDirectory)
    , m_baseUrl(u"https://lrclib.net"_s)
{
    QDir().mkpath(m_directory);
}

void LyricsFetcher::setBaseUrl(const QUrl &baseUrl)
{
    m_baseUrl = baseUrl;
}

QString LyricsFetcher::cacheBase(const Track &track) const
{
    const QString key = u"%1|%2|%3"_s.arg(track.artist, track.title, track.album).toLower();
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(m_directory).filePath(QString::fromLatin1(hash));
}

QString LyricsFetcher::cached(const Track &track, bool *known) const
{
    const QString base = cacheBase(track);
    QFile lyrics(base + u".lrc"_s);
    if (lyrics.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *known = true;
        return QString::fromUtf8(lyrics.readAll());
    }
    const QFileInfo none(base + u".none"_s);
    *known = none.exists() && none.lastModified().secsTo(QDateTime::currentDateTime()) < kNotFoundRetrySecs;
    return {};
}

void LyricsFetcher::store(const Track &track, const QString &lyrics)
{
    const QString base = cacheBase(track);
    QFile file(base + (lyrics.isEmpty() ? u".none"_s : u".lrc"_s));
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        file.write(lyrics.toUtf8());
}

void LyricsFetcher::fetch(const Track &track, std::function<void(const QString &)> onResult)
{
    // Exact match first: LRCLIB matches artist, title, album and length (±2 s).
    QUrlQuery query;
    query.addQueryItem(u"artist_name"_s, track.artist);
    query.addQueryItem(u"track_name"_s, track.title);
    query.addQueryItem(u"album_name"_s, track.album);
    if (track.durationMs > 0)
        query.addQueryItem(u"duration"_s, QString::number(std::lround(track.durationMs / 1000.0)));
    QUrl url = m_baseUrl.resolved(QUrl(u"/api/get"_s));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArray(kUserAgent));
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, track, onResult = std::move(onResult)] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            const QString lyrics = lyricsOf(QJsonDocument::fromJson(reply->readAll()).object());
            store(track, lyrics);
            onResult(lyrics);
            return;
        }
        if (status == 0) { // no connection: try again next time
            onResult({});
            return;
        }
        // Not an exact match (album or length differ): fall back to a search.
        search(track, onResult);
    });
}

void LyricsFetcher::search(const Track &track, std::function<void(const QString &)> onResult)
{
    QUrlQuery query;
    query.addQueryItem(u"artist_name"_s, track.artist);
    query.addQueryItem(u"track_name"_s, track.title);
    QUrl url = m_baseUrl.resolved(QUrl(u"/api/search"_s));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArray(kUserAgent));
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, track, onResult = std::move(onResult)] {
        reply->deleteLater();
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            onResult({}); // service trouble: not cached, retried next time
            return;
        }
        // Prefer a synced version of about the same length, then any synced one, then plain text.
        const QJsonArray results = QJsonDocument::fromJson(reply->readAll()).array();
        const double seconds = track.durationMs / 1000.0;
        QString best;
        int bestScore = 0;
        for (const QJsonValue &value : results) {
            const QJsonObject record = value.toObject();
            const QString lyrics = lyricsOf(record);
            if (lyrics.isEmpty())
                continue;
            const bool synced = !record.value("syncedLyrics"_L1).toString().trimmed().isEmpty();
            const bool sameLength = seconds <= 0 || std::abs(record.value("duration"_L1).toDouble() - seconds) <= 3;
            const int score = (synced ? 2 : 0) + (sameLength ? 1 : 0) + 1;
            if (score > bestScore) {
                best = lyrics;
                bestScore = score;
            }
        }
        store(track, best);
        onResult(best);
    });
}
