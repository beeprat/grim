#include "core/SlskdClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace {

// slskd answers local requests quickly; this only catches a dead or wrong server.
constexpr int kRequestTimeoutMs = 10'000;
// Status is polled; a short timeout keeps checks from piling up.
constexpr int kStatusTimeoutMs = 3'000;

QString suffixOf(const QString &remotePath)
{
    const QString name = remotePath.section(u'\\', -1);
    const qsizetype dot = name.lastIndexOf(u'.');
    return dot < 0 ? QString() : name.mid(dot + 1).toLower();
}

} // namespace

namespace Slskd {

QList<SlskdSearchFile> parseSearchResponses(const QByteArray &json)
{
    QList<SlskdSearchFile> files;
    const QJsonArray responses = QJsonDocument::fromJson(json).array();
    for (const QJsonValue &responseValue : responses) {
        const QJsonObject response = responseValue.toObject();
        const QString username = response.value("username"_L1).toString();
        if (username.isEmpty())
            continue;

        for (const QJsonValue &fileValue : response.value("files"_L1).toArray()) {
            const QJsonObject object = fileValue.toObject();
            const QString filename = object.value("filename"_L1).toString();
            // Locked files need the uploader's permission; they cannot be queued.
            if (filename.isEmpty() || object.value("isLocked"_L1).toBool())
                continue;

            SlskdSearchFile file;
            file.username = username;
            file.filename = filename;
            file.size = object.value("size"_L1).toInteger();
            file.extension = object.value("extension"_L1).toString().toLower();
            if (file.extension.isEmpty())
                file.extension = suffixOf(filename);
            file.bitRate = object.value("bitRate"_L1).toInt();
            file.bitDepth = object.value("bitDepth"_L1).toInt();
            file.sampleRate = object.value("sampleRate"_L1).toInt();
            file.lengthSec = object.value("length"_L1).toInt();
            file.hasFreeSlot = response.value("hasFreeUploadSlot"_L1).toBool();
            file.queueLength = response.value("queueLength"_L1).toInt();
            file.uploadSpeed = response.value("uploadSpeed"_L1).toInteger();
            files.append(file);
        }
    }
    return files;
}

QList<SlskdDownload> parseDownloads(const QByteArray &json)
{
    QList<SlskdDownload> downloads;
    const QJsonArray users = QJsonDocument::fromJson(json).array();
    for (const QJsonValue &userValue : users) {
        const QJsonObject user = userValue.toObject();
        const QString username = user.value("username"_L1).toString();
        for (const QJsonValue &directoryValue : user.value("directories"_L1).toArray()) {
            for (const QJsonValue &fileValue : directoryValue.toObject().value("files"_L1).toArray()) {
                const QJsonObject object = fileValue.toObject();
                SlskdDownload download;
                download.id = object.value("id"_L1).toString();
                download.username = username;
                download.filename = object.value("filename"_L1).toString();
                download.state = object.value("state"_L1).toString();
                download.size = object.value("size"_L1).toInteger();
                download.bytesTransferred = object.value("bytesTransferred"_L1).toInteger();
                if (!download.id.isEmpty())
                    downloads.append(download);
            }
        }
    }
    return downloads;
}

bool isSearchComplete(const QString &state)
{
    for (const auto terminal : {"Completed"_L1, "TimedOut"_L1, "Cancelled"_L1, "Errored"_L1}) {
        if (state.contains(terminal))
            return true;
    }
    return false;
}

bool isTransferFinished(const QString &state)
{
    // slskd prefixes every terminal transfer state with "Completed" (Succeeded, Errored, Cancelled, ...).
    return state.startsWith("Completed"_L1);
}

bool matches(const SlskdSearchFile &file, const SlskdSearchFilter &filter)
{
    if (!filter.extension.isEmpty() && file.extension != filter.extension)
        return false;
    // Unknown bitrate passes: lossless files usually report none.
    if (filter.minBitRate > 0 && file.bitRate > 0 && file.bitRate < filter.minBitRate)
        return false;
    // Exact lossless spec, e.g. 24-bit / 96 kHz. Unknown values fail: the user asked for a specific format.
    if (filter.bitDepth > 0 && file.bitDepth != filter.bitDepth)
        return false;
    if (filter.sampleRate > 0 && file.sampleRate != filter.sampleRate)
        return false;
    return true;
}

QString qualityText(const SlskdSearchFile &file)
{
    if (file.bitRate > 0)
        return u"%1 kbps"_s.arg(file.bitRate);
    if (file.sampleRate > 0) {
        const QString rate = u"%1 kHz"_s.arg(file.sampleRate / 1000.0, 0, 'g', 3);
        return file.bitDepth > 0 ? u"%1-bit · %2"_s.arg(file.bitDepth).arg(rate) : rate;
    }
    return {};
}

FolderNames namesFromFolder(const QString &folderPath)
{
    static const QRegularExpression discFolder(u"^(cd|disc|disk)\\s*\\d+$"_s, QRegularExpression::CaseInsensitiveOption);
    // Folders that say nothing about the artist.
    static const QStringList genericFolders{u"music"_s, u"musik"_s, u"musique"_s, u"mp3"_s, u"flac"_s,
                                            u"albums"_s, u"downloads"_s, u"download"_s, u"complete"_s,
                                            u"completed"_s, u"share"_s, u"shared"_s, u"soulseek"_s, u"media"_s};

    QStringList segments = folderPath.split(u'\\', Qt::SkipEmptyParts);
    FolderNames names;
    if (segments.size() > 1 && discFolder.match(segments.last()).hasMatch())
        segments.removeLast();
    names.albumFolder = segments.join(u'\\');
    if (segments.isEmpty())
        return names;

    names.album = segments.last();
    const QString parent = segments.size() > 1 ? segments.at(segments.size() - 2) : QString();
    const bool parentIsGeneric = parent.isEmpty() || parent.startsWith(u"@@"_s) || parent.endsWith(u':')
                                 || genericFolders.contains(parent.toLower());
    if (!parentIsGeneric) {
        names.artist = parent;
    } else if (const qsizetype dash = names.album.indexOf(u" - "_s); dash > 0) {
        const QString left = names.album.left(dash).trimmed();
        // "1967 - Strange Days": a year, not an artist.
        if (!(left.size() == 4 && left.toInt() > 0))
            names.artist = left;
    }
    return names;
}

QList<Album> groupByAlbum(const QList<SlskdSearchFile> &files)
{
    QList<Album> albums;
    QHash<QString, qsizetype> byKey;
    for (const SlskdSearchFile &file : files) {
        const FolderNames names = namesFromFolder(file.filename.section(u'\\', 0, -2));
        const QString key = file.username + u'\x1f' + names.albumFolder.toLower();
        auto found = byKey.constFind(key);
        if (found == byKey.cend()) {
            found = byKey.insert(key, albums.size());
            albums.append({file.username, names.albumFolder, names.album, names.artist, {}, 0});
        }
        Album &album = albums[*found];
        album.files.append(file);
        album.totalSize += file.size;
    }
    return albums;
}

} // namespace Slskd

SlskdClient::SlskdClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void SlskdClient::setServer(const QUrl &baseUrl, const QString &apiKey)
{
    m_baseUrl = baseUrl;
    m_apiKey = apiKey;
}

bool SlskdClient::isConfigured() const
{
    return m_baseUrl.isValid() && !m_baseUrl.host().isEmpty();
}

void SlskdClient::fetchStatus(std::function<void(SlskdStatus)> onStatus)
{
    QNetworkRequest statusRequest = request(u"application"_s);
    statusRequest.setTransferTimeout(kStatusTimeoutMs);
    QNetworkReply *reply = m_network->get(statusRequest);
    connect(reply, &QNetworkReply::finished, this, [reply, onStatus = std::move(onStatus)] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 || status == 403) {
            onStatus(SlskdStatus::AuthRequired);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            onStatus(SlskdStatus::Offline);
            return;
        }
        const QJsonObject server = QJsonDocument::fromJson(reply->readAll()).object().value("server"_L1).toObject();
        onStatus(server.value("isLoggedIn"_L1).toBool() ? SlskdStatus::Online : SlskdStatus::NotLoggedIn);
    });
}

QNetworkRequest SlskdClient::request(const QString &path) const
{
    // QUrl::StripTrailingSlash keeps a bare "/" path, so trim by hand.
    QString base = m_baseUrl.toString();
    while (base.endsWith(u'/'))
        base.chop(1);
    QNetworkRequest request(QUrl(base + "/api/v0/"_L1 + path));
    if (!m_apiKey.isEmpty())
        request.setRawHeader("X-API-Key", m_apiKey.toUtf8());
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"_ba);
    request.setTransferTimeout(kRequestTimeoutMs);
    return request;
}

void SlskdClient::handle(QNetworkReply *reply, std::function<void(const QByteArray &)> onSuccess)
{
    connect(reply, &QNetworkReply::finished, this, [this, reply, onSuccess = std::move(onSuccess)] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 || status == 403) {
            emit errorOccurred(tr("slskd rejected the API key."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(tr("slskd request failed: %1").arg(reply->errorString()));
            return;
        }
        onSuccess(reply->readAll());
    });
}

void SlskdClient::startSearch(const QString &text, std::function<void(const QString &)> onStarted)
{
    const QJsonObject body{{"searchText"_L1, text}};
    QNetworkReply *reply = m_network->post(request(u"searches"_s), QJsonDocument(body).toJson(QJsonDocument::Compact));
    handle(reply, [this, onStarted = std::move(onStarted)](const QByteArray &response) {
        const QString id = QJsonDocument::fromJson(response).object().value("id"_L1).toString();
        if (id.isEmpty()) {
            emit errorOccurred(tr("slskd returned no search id."));
            return;
        }
        onStarted(id);
    });
}

void SlskdClient::fetchSearchState(const QString &searchId, std::function<void(const QString &, int)> onState)
{
    const QString path = u"searches/"_s + QString::fromUtf8(QUrl::toPercentEncoding(searchId));
    handle(m_network->get(request(path)), [onState = std::move(onState)](const QByteArray &response) {
        const QJsonObject search = QJsonDocument::fromJson(response).object();
        onState(search.value("state"_L1).toString(), search.value("responseCount"_L1).toInt());
    });
}

void SlskdClient::fetchSearchResponses(const QString &searchId, std::function<void(QList<SlskdSearchFile>)> onFiles)
{
    const QString path = u"searches/"_s + QString::fromUtf8(QUrl::toPercentEncoding(searchId)) + u"/responses"_s;
    handle(m_network->get(request(path)), [onFiles = std::move(onFiles)](const QByteArray &response) {
        onFiles(Slskd::parseSearchResponses(response));
    });
}

void SlskdClient::enqueueDownloads(const QList<SlskdSearchFile> &files, std::function<void()> onQueued)
{
    if (files.isEmpty())
        return;
    QJsonArray body;
    for (const SlskdSearchFile &file : files) {
        Q_ASSERT(file.username == files.first().username); // one request is one user
        body.append(QJsonObject{{"filename"_L1, file.filename}, {"size"_L1, file.size}});
    }
    const QString path =
        u"transfers/downloads/"_s + QString::fromUtf8(QUrl::toPercentEncoding(files.first().username));
    QNetworkReply *reply = m_network->post(request(path), QJsonDocument(body).toJson(QJsonDocument::Compact));
    handle(reply, [onQueued = std::move(onQueued)](const QByteArray &) { onQueued(); });
}

void SlskdClient::fetchDownloadsDirectory(std::function<void(const QString &)> onDirectory)
{
    handle(m_network->get(request(u"options"_s)), [onDirectory = std::move(onDirectory)](const QByteArray &response) {
        const QJsonObject directories =
            QJsonDocument::fromJson(response).object().value("directories"_L1).toObject();
        onDirectory(directories.value("downloads"_L1).toString());
    });
}

void SlskdClient::clearFinishedDownloads(std::function<void()> onCleared)
{
    handle(m_network->deleteResource(request(u"transfers/downloads/all/completed"_s)),
           [onCleared = std::move(onCleared)](const QByteArray &) { onCleared(); });
}

void SlskdClient::fetchDownloads(std::function<void(QList<SlskdDownload>)> onDownloads)
{
    handle(m_network->get(request(u"transfers/downloads"_s)),
           [onDownloads = std::move(onDownloads)](const QByteArray &response) {
               onDownloads(Slskd::parseDownloads(response));
           });
}
