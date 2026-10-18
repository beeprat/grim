#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

// One downloadable file from a search response, flattened with its user's info.
struct SlskdSearchFile
{
    QString username;
    QString filename; // full remote path, backslash separated
    qint64 size = 0;
    QString extension; // lower case, no dot
    int bitRate = 0;   // kbps, 0 = unknown (typical for lossless)
    int bitDepth = 0;
    int sampleRate = 0;
    int lengthSec = 0;
    bool hasFreeSlot = false;
    int queueLength = 0;
    qint64 uploadSpeed = 0; // bytes per second
};

struct SlskdDownload
{
    QString id;
    QString username;
    QString filename;
    QString state; // e.g. "Queued, Remotely", "InProgress", "Completed, Succeeded"
    qint64 size = 0;
    qint64 bytesTransferred = 0;
};

struct SlskdSearchFilter
{
    QString extension;  // empty = any
    int minBitRate = 0; // 0 = any; files with unknown bitrate pass
    int bitDepth = 0;   // 0 = any; otherwise exact, unknown fails
    int sampleRate = 0; // 0 = any; otherwise exact, unknown fails
};

enum class SlskdStatus {
    Offline,      // nothing answering at the URL
    AuthRequired, // slskd is running but wants an API key (auth not disabled)
    NotLoggedIn,  // slskd is running but not logged in to the Soulseek server
    Online,
};

namespace Slskd {

// Parsers for slskd JSON. Malformed input gives an empty list, never a crash.
QList<SlskdSearchFile> parseSearchResponses(const QByteArray &json);
QList<SlskdDownload> parseDownloads(const QByteArray &json);

// States are compound flags, e.g. "Completed, ResponseLimitReached".
bool isSearchComplete(const QString &state);
bool isTransferFinished(const QString &state);

bool matches(const SlskdSearchFile &file, const SlskdSearchFilter &filter);

// "320 kbps" for lossy files, "24-bit · 96 kHz" for lossless ones, empty if unknown.
QString qualityText(const SlskdSearchFile &file);

// One user's album folder in the search results.
struct Album
{
    QString username;
    QString folder; // remote folder path (disc folders like "CD1" folded into the album)
    QString title;  // album folder name
    QString artist; // guessed from the path, empty if unknown
    QList<SlskdSearchFile> files;
    qint64 totalSize = 0;
};

// Groups files into albums by user + folder, keeping the order in which albums
// first appear (input is sorted best source first, so albums are too).
QList<Album> groupByAlbum(const QList<SlskdSearchFile> &files);

// Album and artist from a remote folder path, e.g.
// "@@x\Music\The Doors\1967 - Strange Days" -> {"1967 - Strange Days", "The Doors"}
// "@@x\Music\The Doors - Strange Days\CD1" -> {"The Doors - Strange Days", "The Doors"}
struct FolderNames
{
    QString album;
    QString artist;
    QString albumFolder; // folder path without a trailing disc folder
};
FolderNames namesFromFolder(const QString &folderPath);

} // namespace Slskd

// Async client for the slskd REST API (v0). The API key is optional: slskd run
// with authentication disabled (--no-auth) accepts requests without one.
// Callbacks run on the owning thread and only on success; every failure is
// reported once through errorOccurred(). fetchStatus() is the exception: it
// never errors, because "not running" is one of its answers.
class SlskdClient : public QObject
{
    Q_OBJECT

public:
    explicit SlskdClient(QObject *parent = nullptr);

    void setServer(const QUrl &baseUrl, const QString &apiKey);
    bool isConfigured() const;

    void fetchStatus(std::function<void(SlskdStatus)> onStatus);

    void startSearch(const QString &text, std::function<void(const QString &searchId)> onStarted);
    void fetchSearchState(const QString &searchId,
                          std::function<void(const QString &state, int responseCount)> onState);
    void fetchSearchResponses(const QString &searchId, std::function<void(QList<SlskdSearchFile>)> onFiles);
    // Queues files from one user in a single request (e.g. a whole album).
    void enqueueDownloads(const QList<SlskdSearchFile> &files, std::function<void()> onQueued);
    void fetchDownloads(std::function<void(QList<SlskdDownload>)> onDownloads);
    // Removes every finished transfer (succeeded, failed, cancelled) from slskd's list; active ones stay.
    void clearFinishedDownloads(std::function<void()> onCleared);
    // slskd's completed-downloads folder (options: directories.downloads). Needs an admin key or --no-auth.
    void fetchDownloadsDirectory(std::function<void(const QString &path)> onDirectory);

signals:
    void errorOccurred(const QString &message);

private:
    QNetworkRequest request(const QString &path) const;
    void handle(QNetworkReply *reply, std::function<void(const QByteArray &body)> onSuccess);

    QNetworkAccessManager *m_network;
    QUrl m_baseUrl;
    QString m_apiKey;
};
