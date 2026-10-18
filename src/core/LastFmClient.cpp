#include "core/LastFmClient.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrlQuery>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr auto kUserAgent = "GRIM/0.1";
constexpr int kTimeoutMs = 15000;
constexpr qsizetype kBatchSize = 50; // Last.fm's limit per track.scrobble call

// Error codes worth retrying later: service offline, temporarily unavailable, rate limit.
bool isTemporary(int error)
{
    return error == -1 || error == 11 || error == 16 || error == 29;
}

// API key / signature problems: nothing is wrong with the scrobbles themselves.
bool isCredentialError(int error)
{
    return error == 4 || error == 10 || error == 13 || error == 26;
}

QString describe(int error, const QJsonObject &answer)
{
    if (error == -1)
        return u"Cannot reach Last.fm"_s;
    const QString message = answer.value("message"_L1).toString();
    return u"Last.fm: %1 (error %2)"_s.arg(message.isEmpty() ? u"request failed"_s : message).arg(error);
}

} // namespace

LastFmClient::LastFmClient(const QString &queueFile, QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_queueFile(queueFile)
    , m_baseUrl(u"https://ws.audioscrobbler.com/2.0/"_s)
{
    loadQueue();
}

void LastFmClient::setBaseUrl(const QUrl &baseUrl)
{
    m_baseUrl = baseUrl;
}

void LastFmClient::setApiKey(const QString &apiKey, const QString &secret)
{
    m_apiKey = apiKey.trimmed();
    m_secret = secret.trimmed();
}

void LastFmClient::setSessionKey(const QString &sessionKey)
{
    m_sessionKey = sessionKey;
}

bool LastFmClient::hasSession() const
{
    return !m_apiKey.isEmpty() && !m_secret.isEmpty() && !m_sessionKey.isEmpty();
}

QString LastFmClient::signature(const QMap<QString, QString> &params, const QString &secret)
{
    QByteArray text;
    for (auto it = params.cbegin(); it != params.cend(); ++it) { // QMap iterates sorted by key
        if (it.key() == "format"_L1 || it.key() == "callback"_L1)
            continue;
        text += it.key().toUtf8() + it.value().toUtf8();
    }
    text += secret.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(text, QCryptographicHash::Md5).toHex());
}

QUrl LastFmClient::authPage(const QString &apiKey, const QString &token)
{
    QUrl url(u"https://www.last.fm/api/auth/"_s);
    QUrlQuery query;
    query.addQueryItem(u"api_key"_s, apiKey);
    query.addQueryItem(u"token"_s, token);
    url.setQuery(query);
    return url;
}

void LastFmClient::call(Params params, bool signedWithSession,
                        std::function<void(int, const QJsonObject &)> onDone)
{
    params.insert(u"api_key"_s, m_apiKey);
    if (signedWithSession)
        params.insert(u"sk"_s, m_sessionKey);
    params.insert(u"api_sig"_s, signature(params, m_secret));
    params.insert(u"format"_s, u"json"_s);

    // Form encoding by hand: QUrlQuery leaves '+' and '&' inside values ambiguous.
    QByteArray body;
    for (auto it = params.cbegin(); it != params.cend(); ++it) {
        if (!body.isEmpty())
            body += '&';
        body += QUrl::toPercentEncoding(it.key()) + '=' + QUrl::toPercentEncoding(it.value());
    }

    QNetworkRequest request(m_baseUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/x-www-form-urlencoded"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArray(kUserAgent));
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply *reply = m_network->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [reply, onDone = std::move(onDone)] {
        reply->deleteLater();
        // Last.fm answers errors with a 4xx status and a JSON body holding the code.
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
        if (!document.isObject()) {
            onDone(-1, {});
            return;
        }
        const QJsonObject answer = document.object();
        onDone(answer.contains("error"_L1) ? answer.value("error"_L1).toInt(-1) : 0, answer);
    });
}

void LastFmClient::requestToken(std::function<void(const QString &)> onToken)
{
    call({{u"method"_s, u"auth.getToken"_s}}, false, [this, onToken = std::move(onToken)](int error, const QJsonObject &answer) {
        const QString token = error == 0 ? answer.value("token"_L1).toString() : QString();
        if (token.isEmpty())
            emit errorOccurred(describe(error, answer));
        onToken(token);
    });
}

void LastFmClient::requestSession(const QString &token, std::function<void(const QString &, const QString &)> onSession)
{
    call({{u"method"_s, u"auth.getSession"_s}, {u"token"_s, token}}, false,
         [this, onSession = std::move(onSession)](int error, const QJsonObject &answer) {
             const QJsonObject session = answer.value("session"_L1).toObject();
             const QString user = session.value("name"_L1).toString();
             const QString key = session.value("key"_L1).toString();
             if (error != 0 || key.isEmpty())
                 emit errorOccurred(error == 14 ? u"Approve the access on last.fm first, then try again."_s
                                                : describe(error, answer));
             onSession(error == 0 ? user : QString(), error == 0 ? key : QString());
         });
}

void LastFmClient::updateNowPlaying(const Scrobble &track)
{
    if (!hasSession())
        return;
    Params params{{u"method"_s, u"track.updateNowPlaying"_s}, {u"artist"_s, track.artist}, {u"track"_s, track.title}};
    if (!track.album.isEmpty())
        params.insert(u"album"_s, track.album);
    if (track.durationMs > 0)
        params.insert(u"duration"_s, QString::number(track.durationMs / 1000));
    // Best effort: a missed "now playing" is not worth an error message.
    call(params, true, [this](int error, const QJsonObject &) {
        if (error == 9)
            emit sessionRejected();
    });
}

void LastFmClient::enqueue(const Scrobble &scrobble)
{
    m_queue.append(scrobble);
    saveQueue();
    emit pendingCountChanged();
    flush();
}

int LastFmClient::pendingCount() const
{
    return static_cast<int>(m_queue.size());
}

void LastFmClient::flush()
{
    if (m_flushing || m_queue.isEmpty() || !hasSession())
        return;

    const qsizetype count = std::min(m_queue.size(), kBatchSize);
    Params params{{u"method"_s, u"track.scrobble"_s}};
    for (qsizetype i = 0; i < count; ++i) {
        const Scrobble &scrobble = m_queue.at(i);
        const QString suffix = u"[%1]"_s.arg(i);
        params.insert(u"artist"_s + suffix, scrobble.artist);
        params.insert(u"track"_s + suffix, scrobble.title);
        params.insert(u"timestamp"_s + suffix, QString::number(scrobble.timestamp));
        if (!scrobble.album.isEmpty())
            params.insert(u"album"_s + suffix, scrobble.album);
        if (scrobble.durationMs > 0)
            params.insert(u"duration"_s + suffix, QString::number(scrobble.durationMs / 1000));
    }

    m_flushing = true;
    call(params, true, [this, count](int error, const QJsonObject &answer) {
        m_flushing = false;
        if (error == 9) {
            emit sessionRejected();
            return;
        }
        if (isTemporary(error) || isCredentialError(error)) {
            emit errorOccurred(describe(error, answer)); // kept; sent with the next scrobble
            return;
        }
        if (error != 0) // the batch itself is invalid: drop it so it cannot block the queue
            emit errorOccurred(describe(error, answer));
        // Entries added while the request ran sit behind the sent ones.
        m_queue.remove(0, count);
        saveQueue();
        emit pendingCountChanged();
        flush();
    });
}

void LastFmClient::loadQueue()
{
    QFile file(m_queueFile);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray entries = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        Scrobble scrobble;
        scrobble.artist = entry.value("artist"_L1).toString();
        scrobble.title = entry.value("title"_L1).toString();
        scrobble.album = entry.value("album"_L1).toString();
        scrobble.durationMs = entry.value("durationMs"_L1).toInteger();
        scrobble.timestamp = entry.value("timestamp"_L1).toInteger();
        if (!scrobble.artist.isEmpty() && !scrobble.title.isEmpty() && scrobble.timestamp > 0)
            m_queue.append(scrobble);
    }
}

void LastFmClient::saveQueue() const
{
    QJsonArray entries;
    for (const Scrobble &scrobble : m_queue)
        entries.append(QJsonObject{{"artist"_L1, scrobble.artist},
                                   {"title"_L1, scrobble.title},
                                   {"album"_L1, scrobble.album},
                                   {"durationMs"_L1, scrobble.durationMs},
                                   {"timestamp"_L1, scrobble.timestamp}});
    QSaveFile file(m_queueFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(entries).toJson(QJsonDocument::Compact));
        file.commit();
    }
}
