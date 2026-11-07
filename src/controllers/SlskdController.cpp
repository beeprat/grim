#include "controllers/SlskdController.h"

#include "controllers/SettingsController.h"
#include "core/CredentialStore.h"
#include "core/DownloadTagger.h"
#include "core/LibraryFiles.h"
#include "core/TagEditor.h"
#include "core/TagReader.h"

#include <utility>

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QRandomGenerator>
#include <QSettings>
#include <QPointer>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace {

constexpr int kSearchPollMs = 1000;
constexpr int kDownloadsPollMs = 1500;
// Status is checked only while the app starts (slskd may still be launching), then on demand:
// no background polling. kManagedStartupChecks tries, kStatusPollMs apart (slskd starts with grim., and a
// first run sets up its database).
constexpr int kStatusPollMs = 2000;
constexpr int kManagedStartupChecks = 20;

const auto kDownloadFormatKey = "slskd/downloadFormat"_L1;
// searchFiles(): slskd's search timeout (time without new responses), the poll interval, and when a
// running search is stopped so its responses can be read (slskd lists them only once complete):
// after kBatchSearchMs of running, or kBatchSearchEnoughMs with kBatchSearchEnough responses.
constexpr int kBatchSearchTimeoutMs = 8000;
constexpr int kBatchSearchPollMs = 1500;
constexpr int kBatchSearchMs = 20000;
constexpr int kBatchSearchEnoughMs = 8000;
constexpr int kBatchSearchEnough = 60;
const auto kUsernameKey = "slskd/username"_L1;
const auto kManagedApiKeyKey = "slskd/managedApiKey"_L1;
const auto kStoppedKey = "slskd/stopped"_L1;
const auto kPasswordName = "slskd-password"_L1; // CredentialStore
const auto kWebUsernameKey = "slskd/webUsername"_L1;
const auto kWebPasswordName = "slskd-web-password"_L1; // CredentialStore

} // namespace

SlskdController::SlskdController(SettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_client(new SlskdClient(this))
    , m_results(new SearchResultsModel(this))
    , m_groups(new SearchGroupsModel(*m_results, this))
    , m_downloads(new DownloadsModel(this))
    , m_searchTimer(new QTimer(this))
    , m_downloadsTimer(new QTimer(this))
    , m_statusTimer(new QTimer(this))
    , m_runner(new SlskdRunner(this))
    , m_converter(new DownloadConverter(this))
    , m_covers(new CoverFetcher(this))
{
    Q_ASSERT(!s_instance);
    s_instance = this;

    const QSettings stored;
    m_downloadFormat = stored.value(kDownloadFormatKey, u"m4a"_s).toString();
    if (!DownloadConverter::isValidFormat(m_downloadFormat))
        m_downloadFormat = u"m4a"_s;
    m_converter->setFormat(m_downloadFormat);
    m_username = stored.value(kUsernameKey).toString();
    m_stopped = stored.value(kStoppedKey, false).toBool();
    m_webUsername = stored.value(kWebUsernameKey).toString();
    if (!m_webUsername.isEmpty())
        m_runner->setWebLogin(m_webUsername, CredentialStore::read(kWebPasswordName));
    // Only grim. and only this PC use it (slskd accepts it from 127.0.0.1 only), so plain QSettings is fine.
    m_managedApiKey = stored.value(kManagedApiKeyKey).toString();
    if (m_managedApiKey.size() < 32) {
        m_managedApiKey.clear();
        for (int i = 0; i < 4; ++i)
            m_managedApiKey += QString::number(QRandomGenerator::system()->generate64(), 16).rightJustified(16, u'0');
        QSettings().setValue(kManagedApiKeyKey, m_managedApiKey);
    }

    connect(m_runner, &SlskdRunner::stateChanged, this, [this, previous = m_runner->state()]() mutable {
        const SlskdRunner::State state = m_runner->state();
        // Just downloaded: start it. Just started: slskd answers within seconds, so look for it.
        if (managed() && previous == SlskdRunner::Downloading && state == SlskdRunner::Stopped)
            applyServer();
        else if (previous != SlskdRunner::Running && state == SlskdRunner::Running)
            pollStatus(kManagedStartupChecks);
        previous = state;
        emit managedStatusChanged();
    });
    connect(this, &SlskdController::statusChanged, this, &SlskdController::managedStatusChanged);

    m_searchTimer->setInterval(kSearchPollMs);
    connect(m_searchTimer, &QTimer::timeout, this, &SlskdController::pollSearch);
    m_downloadsTimer->setInterval(kDownloadsPollMs);
    connect(m_downloadsTimer, &QTimer::timeout, this, &SlskdController::refreshDownloads);
    connect(m_converter, &DownloadConverter::note, m_downloads, &DownloadsModel::setNote);
    connect(m_converter, &DownloadConverter::ready, this, &SlskdController::importToLibrary);
    connect(m_converter, &DownloadConverter::failed, this, &SlskdController::downloadImportFailed);
    connect(m_converter, &DownloadConverter::ffmpegNeeded, this, &SlskdController::ffmpegNeeded);
    connect(m_converter, &DownloadConverter::installFailed, this, [this](const QString &error) {
        setErrorText(tr("Could not download ffmpeg: %1").arg(error));
    });

    m_statusTimer->setInterval(kStatusPollMs);
    connect(m_statusTimer, &QTimer::timeout, this, [this] {
        if (--m_statusChecksLeft <= 0 || m_status == Online)
            m_statusTimer->stop();
        else
            checkStatus();
    });
    applyServer();

    // Any failure stops polling, so a stopped slskd gives one message, not one per second.
    connect(m_client, &SlskdClient::errorOccurred, this, [this](const QString &message) {
        m_searchTimer->stop();
        m_downloadsTimer->stop();
        if (m_searching) {
            m_searching = false;
            emit searchingChanged();
        }
        setErrorText(message);
        // Without background polling, a failed request is the moment to update the status dot.
        checkStatus();
    });
}

void SlskdController::pollStatus(int checks)
{
    m_statusChecksLeft = checks;
    m_statusTimer->start();
    recheckStatus();
}

void SlskdController::applyServer()
{
    m_client->setServer(QUrl(m_runner->serverUrl()), m_managedApiKey);
    switch (m_runner->state()) {
    case SlskdRunner::NotInstalled:
        if (managed() && !m_stopped)
            m_runner->install(); // starts it once downloaded (stateChanged)
        break;
    case SlskdRunner::Stopped:
        if (managed() && !m_stopped)
            m_runner->start(m_username, CredentialStore::read(kPasswordName), m_managedApiKey);
        break;
    case SlskdRunner::Downloading:
    case SlskdRunner::Running:
    case SlskdRunner::Failed:
        break;
    }
    setStatus(managed() && !m_stopped ? Checking : Offline);
}

void SlskdController::useManaged(const QString &username, const QString &password)
{
    const QString name = username.trimmed();
    if (name.isEmpty())
        return;
    if (!password.isEmpty() && !CredentialStore::write(kPasswordName, password)) {
        setErrorText(tr("Could not store the Soulseek password."));
        return;
    }
    m_username = name;
    m_stopped = false; // saving the login means "start it"
    QSettings settings;
    settings.setValue(kUsernameKey, m_username);
    settings.setValue(kStoppedKey, false);
    setErrorText({});
    emit settingsChanged();
    // slskd reads the login at start: (re)start it, or download it first.
    if (m_runner->state() == SlskdRunner::NotInstalled || m_runner->state() == SlskdRunner::Downloading)
        applyServer();
    else
        restartManaged();
}

void SlskdController::restartManaged()
{
    if (!managed())
        return;
    // start() stops a running copy first; with no install it reports NotInstalled: download it again.
    m_runner->start(m_username, CredentialStore::read(kPasswordName), m_managedApiKey);
    if (m_runner->state() == SlskdRunner::NotInstalled)
        m_runner->install();
    setStatus(Checking);
}

bool SlskdController::managedRunning() const
{
    return !m_stopped && managed();
}

void SlskdController::setManagedRunning(bool running)
{
    if (running == managedRunning() || !managed())
        return;
    m_stopped = !running;
    QSettings().setValue(kStoppedKey, m_stopped);
    if (running) {
        restartManaged();
    } else {
        m_runner->stop();
        setStatus(Offline);
    }
    emit managedStatusChanged();
}

bool SlskdController::webUi() const
{
    return !m_webUsername.isEmpty();
}

QString SlskdController::webUsername() const
{
    return m_webUsername;
}

QString SlskdController::webUrl() const
{
    return m_runner->serverUrl();
}

void SlskdController::setWebUi(bool enabled, const QString &username, const QString &password)
{
    const QString name = enabled ? username.trimmed() : QString();
    if (enabled && name.isEmpty())
        return;
    if (enabled && !password.isEmpty() && !CredentialStore::write(kWebPasswordName, password)) {
        setErrorText(tr("Could not store the web page password."));
        return;
    }
    const QString stored = enabled ? CredentialStore::read(kWebPasswordName) : QString();
    if (enabled && stored.isEmpty())
        return; // a login needs a password
    m_webUsername = name;
    QSettings().setValue(kWebUsernameKey, m_webUsername);
    m_runner->setWebLogin(m_webUsername, stored);
    emit settingsChanged();
    // slskd reads it at start.
    if (managedRunning() && m_runner->state() == SlskdRunner::Running)
        restartManaged();
}

bool SlskdController::managed() const
{
    return !m_username.isEmpty();
}

QString SlskdController::username() const
{
    return m_username;
}

bool SlskdController::managedBusy() const
{
    return m_runner->state() == SlskdRunner::Downloading;
}

bool SlskdController::managedFailed() const
{
    return m_runner->state() == SlskdRunner::Failed;
}

QString SlskdController::managedStatus() const
{
    switch (m_runner->state()) {
    case SlskdRunner::NotInstalled:
        if (!managed())
            return tr("Not connected yet.");
        return tr("slskd %1 will be downloaded from GitHub (about 55 MB).").arg(QLatin1StringView(SlskdRunner::kVersion));
    case SlskdRunner::Downloading:
        return tr("Downloading slskd… %1 %").arg(qRound(m_runner->progress() * 100));
    case SlskdRunner::Stopped:
        return m_stopped ? tr("Stopped: start it from the title bar") : tr("Stopped");
    case SlskdRunner::Failed:
        return m_runner->errorText();
    case SlskdRunner::Running:
        break;
    }
    switch (m_status) {
    case Online:
        return tr("Running, logged in to Soulseek as %1").arg(m_username);
    case NotLoggedIn:
        return tr("Not logged in to Soulseek: that username is taken by someone else, or the password is wrong. Try another username.");
    case Checking:
    case Offline:
    case AuthRequired:
        break;
    }
    return tr("Starting slskd…");
}

void SlskdController::setLibraryTracks(std::function<QList<Track>()> tracks)
{
    m_libraryTracks = std::move(tracks);
}

SlskdController::~SlskdController()
{
    s_instance = nullptr;
}

SlskdController *SlskdController::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "SlskdController::create", "construct SlskdController before loading QML");
    // Owned by main.cpp; stop the QML engine from deleting it.
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QString SlskdController::downloadFormat() const
{
    return m_downloadFormat;
}

void SlskdController::setDownloadFormat(const QString &format)
{
    if (format == m_downloadFormat || !DownloadConverter::isValidFormat(format))
        return;
    m_downloadFormat = format;
    m_converter->setFormat(format);
    QSettings().setValue(kDownloadFormatKey, format);
    emit downloadFormatChanged();
}

SlskdController::Status SlskdController::status() const
{
    return m_status;
}

void SlskdController::checkStatus()
{
    if (!m_client->isConfigured()) {
        setStatus(Offline);
        return;
    }
    m_client->fetchStatus([this](SlskdStatus status) {
        switch (status) {
        case SlskdStatus::Offline:
            setStatus(Offline);
            break;
        case SlskdStatus::AuthRequired:
            setStatus(AuthRequired);
            break;
        case SlskdStatus::NotLoggedIn:
            setStatus(NotLoggedIn);
            break;
        case SlskdStatus::Online:
            setStatus(Online);
            break;
        }
        // A failed request stopped the transfer poll; still reachable, so resume it or queued
        // downloads look stuck forever.
        if ((m_status == Online || m_status == NotLoggedIn) && !m_downloadsTimer->isActive())
            refreshDownloads();
    });
}

void SlskdController::setStatus(Status status)
{
    if (status == m_status)
        return;
    const bool wasReachable = m_status == Online || m_status == NotLoggedIn;
    m_status = status;
    emit statusChanged();
    // slskd just came up (or settings were fixed): drop stale errors and load the queue.
    if (!wasReachable && (status == Online || status == NotLoggedIn)) {
        setErrorText({});
        m_client->fetchDownloadsDirectory([this](const QString &directory) {
            m_downloadsDirectory = directory;
            refreshDownloads();
        });
    }
}

bool SlskdController::searching() const
{
    return m_searching;
}

int SlskdController::responseCount() const
{
    return m_responseCount;
}

QString SlskdController::errorText() const
{
    return m_errorText;
}

SearchResultsModel *SlskdController::results() const
{
    return m_results;
}

SearchGroupsModel *SlskdController::groups() const
{
    return m_groups;
}

DownloadsModel *SlskdController::downloads() const
{
    return m_downloads;
}

void SlskdController::recheckStatus()
{
    setStatus(Checking);
    checkStatus();
}

void SlskdController::search(const QString &text)
{
    const QString query = text.trimmed();
    if (query.isEmpty() || m_status != Online)
        return;

    setErrorText({});
    m_searchId.clear();
    m_searchTimer->stop();
    m_results->setFiles({});
    m_responseCount = 0;
    m_searching = true;
    emit searchingChanged();

    m_client->startSearch(query, [this](const QString &searchId) {
        m_searchId = searchId;
        m_searchTimer->start();
    });
}

void SlskdController::pollSearch()
{
    const QString searchId = m_searchId;
    m_client->fetchSearchState(searchId, [this, searchId](const QString &state, int responseCount) {
        if (searchId != m_searchId)
            return;
        const bool complete = Slskd::isSearchComplete(state);
        // Responses only change when the count does; skip the larger request otherwise.
        if (responseCount != m_responseCount || complete) {
            m_responseCount = responseCount;
            emit searchingChanged();
            m_client->fetchSearchResponses(searchId, [this, searchId](QList<SlskdSearchFile> files) {
                // Results only grow; a smaller list is an older reply arriving late.
                if (searchId == m_searchId && files.size() >= m_results->totalCount())
                    m_results->setFiles(std::move(files));
            });
        }
        if (complete)
            finishSearch();
    });
}

void SlskdController::finishSearch()
{
    m_searchTimer->stop();
    m_searching = false;
    emit searchingChanged();
}

void SlskdController::download(int resultRow)
{
    const SlskdSearchFile *file = m_results->fileAt(resultRow);
    if (!file)
        return;
    setErrorText({});
    m_client->enqueueDownloads({*file}, [this] { refreshDownloads(); });
}

void SlskdController::downloadAlbum(int albumIndex)
{
    const Slskd::Album *album = m_groups->albumAt(albumIndex);
    if (!album)
        return;
    setErrorText({});
    m_client->enqueueDownloads(album->files, [this] { refreshDownloads(); });
}

void SlskdController::downloadAlbumFile(int albumIndex, int fileIndex)
{
    const Slskd::Album *album = m_groups->albumAt(albumIndex);
    if (!album || fileIndex < 0 || fileIndex >= album->files.size())
        return;
    setErrorText({});
    m_client->enqueueDownloads({album->files.at(fileIndex)}, [this] { refreshDownloads(); });
}

void SlskdController::refreshDownloads()
{
    if (m_status != Online && m_status != NotLoggedIn)
        return;
    m_client->fetchDownloads([this](QList<SlskdDownload> downloads) {
        // Listeners learn the download ids first: "Keep original" imports inside convertFinished.
        emit transfersUpdated(downloads);
        convertFinished(downloads);
        QString current;
        int others = 0;
        for (const SlskdDownload &download : std::as_const(downloads)) {
            if (!download.state.contains("InProgress"_L1) || download.size <= 0)
                continue;
            if (current.isEmpty())
                current = u"%1  ·  %2 %"_s.arg(download.filename.section(u'\\', -1))
                              .arg(download.bytesTransferred * 100 / download.size);
            else
                ++others;
        }
        if (others > 0)
            current += u"  +%1"_s.arg(others);
        if (current != m_currentDownload) {
            m_currentDownload = current;
            emit currentDownloadChanged();
        }
        m_downloads->setDownloads(std::move(downloads));
        // Keep polling only while something is still moving.
        if (m_downloads->hasActive())
            m_downloadsTimer->start();
        else
            m_downloadsTimer->stop();
    });
}

void SlskdController::convertFinished(const QList<SlskdDownload> &downloads)
{
    QList<const SlskdDownload *> fresh;
    for (const SlskdDownload &download : downloads) {
        if (download.state.contains("Succeeded"_L1) && !m_handledDownloads.contains(download.id))
            fresh.append(&download);
    }
    if (fresh.isEmpty() || m_downloadsDirectory.isEmpty())
        return;

    // slskd's API does not report where a file landed (subfolder templates, rename on clash),
    // so find it by name in the downloads folder, newest first.
    QHash<QString, QFileInfo> byName;
    QDirIterator it(m_downloadsDirectory, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const auto existing = byName.constFind(info.fileName());
        if (existing == byName.cend() || existing->lastModified() < info.lastModified())
            byName.insert(info.fileName(), info);
    }

    for (const SlskdDownload *download : fresh) {
        m_handledDownloads.insert(download->id);
        const QString name = download->filename.section(u'\\', -1);
        const auto found = byName.constFind(name);
        if (found == byName.cend()) {
            // Also the case for downloads already moved into the library in an earlier session.
            m_downloads->setNote(download->id, tr("Not in slskd's downloads folder"));
            emit downloadImportFailed(download->id, tr("Not in slskd's downloads folder"));
            continue;
        }
        m_converter->process(found->absoluteFilePath(), download->id);
    }
}

bool SlskdController::canDownloadFfmpeg() const
{
    return DownloadConverter::canDownloadFfmpeg();
}

void SlskdController::installFfmpeg()
{
    setErrorText({});
    m_converter->installFfmpeg();
}

void SlskdController::keepOriginals()
{
    m_converter->keepOriginals();
}

void SlskdController::importToLibrary(const QString &path, const QString &downloadId, const QString &doneNote)
{
    // Join the album already in the library: its folder, artist, album name, year and cover. Edition notes
    // ("(Remastered 2009)") leave the title either way.
    const Track tags = TagReader::read(path);
    const DownloadTagger::AlbumMatch match =
        DownloadTagger::findAlbum(tags, m_libraryTracks ? m_libraryTracks() : QList<Track>());
    const QString title = DownloadTagger::cleanTitle(tags.title);
    // shortcut: covers are read on the GUI thread (one album's files); move to a worker if imports stutter.
    const QByteArray cover = match.found ? DownloadTagger::majorityCover(match.paths) : QByteArray();
    const bool retag = title != tags.title || match.found;
    if (retag && !DownloadTagger::writeTags(path, title, match.artist, match.album, match.year, cover))
        qWarning() << "SlskdController: could not retag" << path;

    // No track number in the file name ("01 Help!.m4a"): it is in the tags. Renamed in the download folder,
    // so the move below keeps its no-overwrite handling.
    QString source = path;
    const QFileInfo info(path);
    const QString fileName = DownloadTagger::cleanFileName(info.fileName(), tags.trackNumber);
    if (fileName != info.fileName() && !QFileInfo::exists(info.dir().filePath(fileName))
        && QFile::rename(path, info.dir().filePath(fileName)))
        source = info.dir().filePath(fileName);

    // A new album goes into <Artist>/<Album> (existing folders reused); without an artist tag it
    // keeps its download folder as before.
    QString error;
    const QString folder = match.found ? match.folder
                                       : DownloadTagger::libraryFolderFor(m_settings.libraryFolder(), tags.artist, tags.album);
    const QString moved = !folder.isEmpty()
                              ? LibraryFiles::moveIntoFolder(source, folder, &error)
                              : LibraryFiles::moveIntoLibrary(source, m_downloadsDirectory, m_settings.libraryFolder(), &error);
    if (moved.isEmpty()) {
        m_downloads->setNote(downloadId, tr("Not added to the library: %1").arg(error));
        emit downloadImportFailed(downloadId, tr("Not added to the library: %1").arg(error));
        return;
    }
    const auto announce = [this, downloadId, doneNote, moved] {
        m_downloads->setNote(downloadId, doneNote);
        emit trackImported(moved);
        emit downloadImported(downloadId, moved);
        const Track added = TagReader::read(moved);
        emit songAdded(added.artist, added.title.isEmpty() ? QFileInfo(moved).completeBaseName() : added.title);
    };
    // No cover in the file, next to it or on the album it joined: look one up online first.
    if (!cover.isEmpty() || !TagReader::readCover(moved).isEmpty()) {
        announce();
        return;
    }
    const Track placed = TagReader::read(moved);
    m_downloads->setNote(downloadId, tr("Looking for the album cover…"));
    m_covers->fetch(placed.artist, placed.album,
                    [moved, announce](const QByteArray &found) {
                        if (!found.isEmpty()) {
                            const QString error = TagEditor::write(moved, {}, TagEditor::Cover::Replace, found);
                            if (!error.isEmpty())
                                qWarning() << "SlskdController: could not add the cover to" << moved << error;
                        }
                        announce();
                    });
}

void SlskdController::clearFinishedDownloads()
{
    setErrorText({});
    m_client->clearFinishedDownloads([this] { refreshDownloads(); });
}

void SlskdController::cancelDownload(int row)
{
    const SlskdDownload download = m_downloads->at(row);
    if (download.id.isEmpty())
        return;
    m_client->cancelDownload(download, [this] { refreshDownloads(); });
}

void SlskdController::cancelTransfer(const SlskdDownload &download)
{
    m_client->cancelDownload(download, [this] { refreshDownloads(); });
}

void SlskdController::setErrorText(const QString &text)
{
    if (text == m_errorText)
        return;
    m_errorText = text;
    emit errorTextChanged();
}

void SlskdController::searchFiles(const QString &text, std::function<void(QList<SlskdSearchFile>)> onFiles)
{
    if (m_status != Online) {
        onFiles({});
        return;
    }
    m_batchSearches.append({text, std::move(onFiles), 0});
    startNextBatchSearch();
}

void SlskdController::startNextBatchSearch()
{
    if (m_batchStarting || m_batchSearches.isEmpty())
        return;
    m_batchStarting = true;
    BatchSearch next = m_batchSearches.takeFirst();
    m_client->startSearch(
        next.text,
        [this, onFiles = next.onFiles](const QString &searchId) {
            m_batchStarting = false;
            startNextBatchSearch();
            collectBatchSearch(searchId, onFiles);
        },
        kBatchSearchTimeoutMs,
        [this, next](int status) mutable {
            m_batchStarting = false;
            // Busy: try again shortly, behind the others.
            if (status == 429 && ++next.attempts < 10) {
                m_batchSearches.append(next);
                QTimer::singleShot(1000, this, &SlskdController::startNextBatchSearch);
                return;
            }
            qWarning() << "SlskdController: batch search did not start, HTTP" << status;
            next.onFiles({});
            startNextBatchSearch();
        });
}

void SlskdController::collectBatchSearch(const QString &searchId, std::function<void(QList<SlskdSearchFile>)> onFiles)
{
    // Polls until slskd calls the search complete (it lists the responses only then), at most
    // kBatchSearchMs. A complete search with responses but an empty list is read again once: the
    // responses are stored a moment after the state changes.
    QPointer<QTimer> timer = new QTimer(this);
    timer->setInterval(kBatchSearchPollMs);
    const auto finish = [this, searchId, onFiles, timer](int responseCount) {
        if (!timer || !timer->isActive())
            return; // already finished
        timer->stop();
        timer->deleteLater();
        const auto read = [this, searchId, onFiles](bool again, auto &self) -> void {
            m_client->fetchSearchResponses(
                searchId,
                [this, onFiles, again, self](QList<SlskdSearchFile> files) {
                    if (files.isEmpty() && again) {
                        QTimer::singleShot(2000, this, [self] { self(false, self); });
                        return;
                    }
                    onFiles(std::move(files));
                },
                [onFiles](int) { onFiles({}); });
        };
        read(responseCount > 0, read);
    };
    connect(timer, &QTimer::timeout, this, [this, searchId, timer, finish] {
        m_client->fetchSearchState(searchId, [this, searchId, timer, finish](const QString &state, int responseCount) {
            if (!timer || !timer->isActive())
                return;
            if (Slskd::isSearchComplete(state)) {
                finish(responseCount);
                return;
            }
            // Time counts only while running: slskd queues searches beyond a few at once.
            if (!state.contains(u"InProgress"_s))
                return;
            const int running = timer->property("running").toInt() + kBatchSearchPollMs;
            timer->setProperty("running", running);
            if (running >= kBatchSearchMs || (running >= kBatchSearchEnoughMs && responseCount >= kBatchSearchEnough)) {
                if (!timer->property("stopping").toBool()) {
                    timer->setProperty("stopping", true);
                    m_client->stopSearch(searchId, [] {}); // the next poll sees it complete
                }
            }
        });
    });
    timer->start();
}

void SlskdController::enqueueFile(const SlskdSearchFile &file)
{
    m_client->enqueueDownloads({file}, [this] { refreshDownloads(); });
}
