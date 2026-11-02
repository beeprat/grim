#include "controllers/SpotifyController.h"

#include "controllers/LibraryController.h"
#include "controllers/PlaylistController.h"
#include "controllers/SlskdController.h"
#include "core/TrackPicker.h"
#include "core/SpotifyMatch.h"
#include "providers/LibraryModel.h"

#include <QJSEngine>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDateTime>
#include <QSettings>
#include <QTimer>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace {

const QString kImportedGroup = u"spotify/imported/"_s;
// A batch search that has not answered by then counts as finished without results.
constexpr qint64 kSearchTimeoutMs = 120000;
// A queued file slskd never lists (the uploader went away) is given up after this; the next result follows.
constexpr qint64 kEnqueueTimeoutMs = 60000;
// Downloaded but never imported (conversion stuck) after this: failed.
constexpr qint64 kAddingTimeoutMs = 10 * 60 * 1000;

} // namespace

SpotifyController::SpotifyController(LibraryController &library, PlaylistController &playlists, SlskdController &slskd,
                                     QObject *parent)
    : QObject(parent)
    , m_library(library)
    , m_playlists(playlists)
    , m_slskd(slskd)
    , m_network(new QNetworkAccessManager(this))
    , m_downloads(new PlaylistDownloadsModel(this))
    , m_watchdog(new QTimer(this))
{
    Q_ASSERT(!s_instance);
    s_instance = this;
    connect(&m_slskd, &SlskdController::transfersUpdated, this, &SpotifyController::onTransfers);
    connect(&m_slskd, &SlskdController::downloadImported, this, &SpotifyController::onImported);
    connect(&m_slskd, &SlskdController::downloadImportFailed, this, &SpotifyController::onImportFailed);
    connect(&m_slskd, &SlskdController::statusChanged, this, &SpotifyController::startWhenOnline);
    m_watchdog->setInterval(5000);
    connect(m_watchdog, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (int row = 0; row < m_items.size(); ++row) {
            const PlaylistDownloadsModel::State state = m_downloads->at(row).state;
            const Item &item = m_items.at(row);
            if (now <= item.deadline)
                continue;
            if (state == PlaylistDownloadsModel::Searching)
                m_downloads->update(row, PlaylistDownloadsModel::NotFound, tr("Search got no answer"));
            else if (state == PlaylistDownloadsModel::Queued && item.downloadId.isEmpty())
                queueNext(row);
            else if (state == PlaylistDownloadsModel::Adding)
                m_downloads->update(row, PlaylistDownloadsModel::Failed, tr("Downloaded, but not added to the library"));
        }
        pump();
        // Downloads also report through the transfer list; keep it coming while waiting.
        m_slskd.refreshDownloads();
    });
}

SpotifyController::~SpotifyController()
{
    s_instance = nullptr;
}

SpotifyController *SpotifyController::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "SpotifyController::create", "construct SpotifyController before loading QML");
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

bool SpotifyController::busy() const { return m_busy; }
PlaylistDownloadsModel *SpotifyController::downloads() const { return m_downloads; }
bool SpotifyController::downloading() const { return m_running; }

int SpotifyController::downloadedCount() const
{
    int count = 0;
    for (const Item &item : m_items) {
        if (!item.path.isEmpty())
            ++count;
    }
    return count;
}
QString SpotifyController::errorText() const { return m_errorText; }
QVariantMap SpotifyController::lastImport() const { return m_lastImport; }

void SpotifyController::importLink(const QString &link)
{
    if (m_busy || m_running)
        return;
    setErrorText({});
    const Spotify::Link parsed = Spotify::parseLink(link);
    if (parsed.kind.isEmpty()) {
        setErrorText(tr("That is not a Spotify playlist or album link."));
        return;
    }

    QNetworkRequest request(QUrl(u"https://open.spotify.com/embed/%1/%2"_s.arg(parsed.kind, parsed.id)));
    // The embed page is served to browsers; without one it may answer differently.
    request.setHeader(QNetworkRequest::UserAgentHeader, u"Mozilla/5.0 (X11; Linux x86_64) GRIM/0.1"_s);
    QNetworkReply *reply = m_network->get(request);
    setBusy(true);
    connect(reply, &QNetworkReply::finished, this, [this, reply, parsed] {
        reply->deleteLater();
        setBusy(false);
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            setErrorText(status == 404 ? tr("Spotify has no such playlist, or it is private.")
                                       : tr("Could not reach Spotify: %1").arg(reply->errorString()));
            return;
        }
        QString name;
        QList<Spotify::RemoteTrack> tracks;
        if (!Spotify::parseEmbedPage(reply->readAll(), &name, &tracks)) {
            setErrorText(tr("Could not read that Spotify page. It may be private."));
            return;
        }

        const Spotify::Matcher matcher(m_library.tracks()->tracks());
        QList<QUrl> matched;
        QVariantList missing;
        m_spotifyId = parsed.id;
        m_name = name.isEmpty() ? tr("Spotify playlist") : name;
        m_tracks = tracks;
        m_paths.clear();
        m_items.clear();
        m_downloads->setRows({});
        for (const Spotify::RemoteTrack &remote : std::as_const(tracks)) {
            const QString path = matcher.match(remote);
            m_paths.append(path);
            if (path.isEmpty()) {
                missing.append(QVariantMap{{u"title"_s, remote.title}, {u"artist"_s, remote.artists.join(u", "_s)}});
                continue;
            }
            const QUrl fileUrl = QUrl::fromLocalFile(path);
            if (!matched.contains(fileUrl))
                matched.append(fileUrl);
        }

        // Importing again replaces the playlist made last time.
        qint64 playlistId = -1;
        if (!matched.isEmpty()) {
            QSettings settings;
            const QString key = kImportedGroup + parsed.id;
            if (settings.contains(key))
                m_playlists.deletePlaylist(settings.value(key).toLongLong());
            playlistId = m_playlists.createPlaylist(m_name, matched);
            if (playlistId > 0)
                settings.setValue(key, playlistId);
        }

        m_lastImport = {{u"name"_s, name},
                        {u"playlistId"_s, playlistId},
                        {u"matched"_s, int(tracks.size() - missing.size())},
                        {u"total"_s, int(tracks.size())},
                        {u"missing"_s, missing}};
        emit lastImportChanged();
    });
}

void SpotifyController::clearResult()
{
    if (m_running)
        return;
    m_items.clear();
    m_downloads->setRows({});
    if (m_lastImport.isEmpty() && m_errorText.isEmpty())
        return;
    m_lastImport.clear();
    emit lastImportChanged();
    setErrorText({});
}

void SpotifyController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void SpotifyController::setErrorText(const QString &text)
{
    if (m_errorText == text)
        return;
    m_errorText = text;
    emit errorTextChanged();
}

void SpotifyController::downloadMissing(const QString &searchFormat, const QString &saveFormat)
{
    if (m_running || m_tracks.isEmpty())
        return;
    setErrorText({});
    if (!m_slskd.managed()) {
        setErrorText(tr("Set your Soulseek login in Settings → Soulseek first."));
        return;
    }
    m_slskd.setDownloadFormat(saveFormat);
    m_searchFormat = searchFormat;

    QList<PlaylistDownloadsModel::Row> rows;
    m_items.clear();
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (!m_paths.at(i).isEmpty())
            continue;
        Item item;
        item.order = i;
        item.track = m_tracks.at(i);
        m_items.append(item);
        rows.append({item.track.title, item.track.artists.join(u", "_s), PlaylistDownloadsModel::Waiting, {}});
    }
    if (m_items.isEmpty())
        return;
    m_downloads->setRows(rows);
    m_running = true;
    emit downloadingChanged();
    m_watchdog->start();

    if (m_slskd.status() == SlskdController::Online) {
        searchAlbums();
        pump();
        return;
    }
    // Start grim.'s slskd; the status change brings it back here.
    m_waitingForSlskd = true;
    for (int row = 0; row < m_items.size(); ++row)
        m_downloads->update(row, PlaylistDownloadsModel::Waiting, tr("Starting Soulseek…"));
    if (!m_slskd.managedRunning())
        m_slskd.setManagedRunning(true);
    else
        m_slskd.recheckStatus();
}

void SpotifyController::startWhenOnline()
{
    if (!m_waitingForSlskd || m_slskd.status() != SlskdController::Online)
        return;
    m_waitingForSlskd = false;
    for (int row = 0; row < m_items.size(); ++row)
        m_downloads->update(row, PlaylistDownloadsModel::Waiting);
    searchAlbums();
    pump();
}

int SpotifyController::activeSearches() const
{
    int count = 0;
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_downloads->at(row).state == PlaylistDownloadsModel::Searching)
            ++count;
    }
    return count;
}

void SpotifyController::pump()
{
    if (!m_running || m_waitingForSlskd)
        return;
    for (int row = 0; row < m_items.size() && activeSearches() < kParallelSearches; ++row) {
        if (m_downloads->at(row).state == PlaylistDownloadsModel::Waiting)
            search(row);
    }
    checkFinished();
}

void SpotifyController::search(int row, int attempt)
{
    Item &item = m_items[row];
    const QStringList texts = TrackPicker::searchTexts(item.track);
    if (attempt == 0) {
        item.candidates.clear();
        item.tried = 0;
        item.deadline = QDateTime::currentMSecsSinceEpoch() + kSearchTimeoutMs * texts.size();
    }
    const QString text = texts.at(attempt);
    m_downloads->update(row, PlaylistDownloadsModel::Searching, text);
    m_slskd.searchFiles(text, [this, row, text, attempt, texts](QList<SlskdSearchFile> files) {
        if (row >= m_items.size() || m_downloads->at(row).state != PlaylistDownloadsModel::Searching
            || m_downloads->at(row).detail != text)
            return; // timed out, stopped or a newer search
        // Nothing at all (a blocked artist name?): the next way of searching.
        if (files.isEmpty() && attempt + 1 < texts.size()) {
            search(row, attempt + 1);
            return;
        }
        Item &item = m_items[row];
        for (const int index : TrackPicker::rank(files, item.track, m_searchFormat))
            item.candidates.append(files.at(index));
        if (item.candidates.isEmpty())
            m_downloads->update(row, PlaylistDownloadsModel::NotFound,
                                files.isEmpty() ? tr("No results") : tr("No result matched this song"));
        else
            queueNext(row);
        pump();
    });
}

void SpotifyController::searchAlbums()
{
    // Rows by album + first artist; albums with enough missing songs get one search.
    QHash<QString, QList<int>> groups;
    QStringList order;
    for (int row = 0; row < m_items.size(); ++row) {
        const Spotify::RemoteTrack &track = m_items.at(row).track;
        if (track.album.isEmpty() || track.artists.isEmpty() || m_downloads->at(row).state != PlaylistDownloadsModel::Waiting)
            continue;
        const QString key = Spotify::normalize(track.artists.first()) + u'\x1f' + Spotify::baseTitle(track.album);
        if (!groups.contains(key))
            order.append(key);
        groups[key].append(row);
    }
    for (const QString &key : std::as_const(order)) {
        const QList<int> rows = groups.value(key);
        if (rows.size() < kAlbumSearchMin)
            continue;
        const Spotify::RemoteTrack &first = m_items.at(rows.first()).track;
        searchAlbum(rows, TrackPicker::albumSearchTexts(first.artists.first(), first.album), 0);
    }
}

void SpotifyController::searchAlbum(const QList<int> &rows, const QStringList &texts, int attempt)
{
    const QString text = tr("Album: %1").arg(texts.at(attempt));
    for (const int row : rows) {
        Item &item = m_items[row];
        item.candidates.clear();
        item.tried = 0;
        item.deadline = QDateTime::currentMSecsSinceEpoch() + kSearchTimeoutMs * texts.size();
        m_downloads->update(row, PlaylistDownloadsModel::Searching, text);
    }
    m_slskd.searchFiles(texts.at(attempt), [this, rows, texts, attempt, text](QList<SlskdSearchFile> files) {
        QList<int> live; // rows still waiting for this search
        for (const int row : rows) {
            if (row < m_items.size() && m_downloads->at(row).state == PlaylistDownloadsModel::Searching
                && m_downloads->at(row).detail == text)
                live.append(row);
        }
        if (live.isEmpty())
            return;
        if (files.isEmpty() && attempt + 1 < texts.size()) {
            searchAlbum(live, texts, attempt + 1);
            return;
        }
        for (const int row : std::as_const(live)) {
            Item &item = m_items[row];
            for (const int index : TrackPicker::rank(files, item.track, m_searchFormat))
                item.candidates.append(files.at(index));
            if (item.candidates.isEmpty())
                m_downloads->update(row, PlaylistDownloadsModel::Waiting); // searched on its own next
            else
                queueNext(row);
        }
        pump();
    });
}

void SpotifyController::queueNext(int row)
{
    Item &item = m_items[row];
    if (item.tried >= item.candidates.size()) {
        m_downloads->update(row, PlaylistDownloadsModel::Failed, tr("Every result failed"));
        checkFinished();
        return;
    }
    const SlskdSearchFile &file = item.candidates.at(item.tried++);
    item.username = file.username;
    item.filename = file.filename;
    item.downloadId.clear();
    item.path.clear();
    const QString shown = file.filename.section(u'\\', -1);
    const QString quality = Slskd::qualityText(file);
    item.deadline = QDateTime::currentMSecsSinceEpoch() + kEnqueueTimeoutMs;
    m_downloads->update(row, PlaylistDownloadsModel::Queued,
                        quality.isEmpty() ? shown : shown + u"  ·  "_s + quality);
    m_slskd.enqueueFile(file);
}

void SpotifyController::onTransfers(const QList<SlskdDownload> &downloads)
{
    if (!m_running)
        return;
    for (int row = 0; row < m_items.size(); ++row) {
        const PlaylistDownloadsModel::State state = m_downloads->at(row).state;
        if (state != PlaylistDownloadsModel::Queued && state != PlaylistDownloadsModel::Downloading)
            continue;
        Item &item = m_items[row];
        for (const SlskdDownload &download : downloads) {
            if (download.username != item.username || download.filename != item.filename)
                continue;
            item.downloadId = download.id;
            const QString name = item.filename.section(u'\\', -1);
            if (download.state.contains(u"Succeeded"_s)) {
                item.deadline = QDateTime::currentMSecsSinceEpoch() + kAddingTimeoutMs;
                m_downloads->update(row, PlaylistDownloadsModel::Adding, tr("Adding to the library…"));
            } else if (Slskd::isTransferFinished(download.state)) {
                queueNext(row); // errored, rejected, timed out or cancelled: the next result
            } else if (download.state.contains(u"InProgress"_s) && download.size > 0) {
                m_downloads->update(row, PlaylistDownloadsModel::Downloading,
                                    u"%1  ·  %2 %"_s.arg(name).arg(download.bytesTransferred * 100 / download.size));
            }
            break;
        }
    }
}

void SpotifyController::onImported(const QString &downloadId, const QString &path)
{
    for (int row = 0; row < m_items.size(); ++row) {
        Item &item = m_items[row];
        if (item.downloadId != downloadId || downloadId.isEmpty())
            continue;
        item.path = path;
        m_paths[item.order] = path;
        m_downloads->update(row, PlaylistDownloadsModel::Done, path.section(u'/', -1));
        emit downloadingChanged(); // downloadedCount
        if (m_running)
            checkFinished();
        else
            rebuildPlaylist(); // finished after Stop: still goes into the playlist
        return;
    }
}

void SpotifyController::onImportFailed(const QString &downloadId, const QString &reason)
{
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items.at(row).downloadId == downloadId && !downloadId.isEmpty()) {
            m_downloads->update(row, PlaylistDownloadsModel::Failed, reason);
            checkFinished();
            return;
        }
    }
}

void SpotifyController::checkFinished()
{
    if (!m_running)
        return;
    for (int row = 0; row < m_items.size(); ++row) {
        switch (m_downloads->at(row).state) {
        case PlaylistDownloadsModel::Done:
        case PlaylistDownloadsModel::NotFound:
        case PlaylistDownloadsModel::Failed:
            break;
        default:
            return; // still going
        }
    }
    m_running = false;
    m_watchdog->stop();
    rebuildPlaylist();
    emit downloadingChanged();
}

void SpotifyController::retry(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    const PlaylistDownloadsModel::State state = m_downloads->at(row).state;
    Item &item = m_items[row];
    if (state == PlaylistDownloadsModel::Done) {
        // The wrong file: drop it from the playlist and fetch the next result.
        m_paths[item.order].clear();
        item.path.clear();
    } else if (state != PlaylistDownloadsModel::NotFound && state != PlaylistDownloadsModel::Failed) {
        return;
    }
    if (!m_running) {
        m_running = true;
        m_watchdog->start();
        emit downloadingChanged();
    }
    if (state == PlaylistDownloadsModel::Done && item.tried >= item.candidates.size()) {
        // A new search would pick the same file again.
        m_downloads->update(row, PlaylistDownloadsModel::Failed, tr("No other result"));
        checkFinished();
        return;
    }
    if (state == PlaylistDownloadsModel::Done)
        queueNext(row);
    else
        m_downloads->update(row, PlaylistDownloadsModel::Waiting); // a fresh search
    if (m_slskd.status() != SlskdController::Online) {
        m_waitingForSlskd = true;
        if (!m_slskd.managedRunning())
            m_slskd.setManagedRunning(true);
        else
            m_slskd.recheckStatus();
        return;
    }
    pump();
}

void SpotifyController::stopDownloads()
{
    if (!m_running)
        return;
    // Transfers already queued in slskd keep going and still land in the library.
    for (int row = 0; row < m_items.size(); ++row) {
        const PlaylistDownloadsModel::State state = m_downloads->at(row).state;
        if (state == PlaylistDownloadsModel::Waiting || state == PlaylistDownloadsModel::Searching)
            m_downloads->update(row, PlaylistDownloadsModel::NotFound, tr("Stopped"));
    }
    m_waitingForSlskd = false;
    m_running = false;
    m_watchdog->stop();
    rebuildPlaylist();
    emit downloadingChanged();
}

void SpotifyController::rebuildPlaylist()
{
    QList<QUrl> urls;
    for (const QString &path : std::as_const(m_paths)) {
        if (!path.isEmpty() && !urls.contains(QUrl::fromLocalFile(path)))
            urls.append(QUrl::fromLocalFile(path));
    }
    if (urls.isEmpty())
        return;
    QSettings settings;
    const QString key = kImportedGroup + m_spotifyId;
    const bool wasCurrent = settings.contains(key) && m_playlists.currentId() == settings.value(key).toLongLong();
    if (settings.contains(key))
        m_playlists.deletePlaylist(settings.value(key).toLongLong());
    const qint64 id = m_playlists.createPlaylist(m_name, urls);
    if (id > 0) {
        settings.setValue(key, id);
        m_lastImport.insert(u"playlistId"_s, id);
        m_lastImport.insert(u"matched"_s, int(urls.size()));
        emit lastImportChanged();
        if (wasCurrent)
            m_playlists.setCurrentId(id);
    }
}
