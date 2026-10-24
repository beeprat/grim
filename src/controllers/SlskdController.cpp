#include "controllers/SlskdController.h"

#include "controllers/SettingsController.h"
#include "core/CredentialStore.h"
#include "core/DownloadTagger.h"
#include "core/LibraryFiles.h"
#include "core/TagReader.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QRandomGenerator>
#include <QSettings>
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
const auto kOriginalFormat = "original"_L1;
const auto kUsernameKey = "slskd/username"_L1;
const auto kManagedApiKeyKey = "slskd/managedApiKey"_L1;
const auto kStoppedKey = "slskd/stopped"_L1;
const auto kPasswordName = "slskd-password"_L1; // CredentialStore

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
    , m_converter(new AudioConverter(this))
    , m_runner(new SlskdRunner(this))
{
    Q_ASSERT(!s_instance);
    s_instance = this;

    const QSettings stored;
    m_downloadFormat = stored.value(kDownloadFormatKey, u"m4a"_s).toString();
    if (m_downloadFormat != kOriginalFormat && !AudioConverter::formats().contains(m_downloadFormat))
        m_downloadFormat = u"m4a"_s;
    m_username = stored.value(kUsernameKey).toString();
    m_stopped = stored.value(kStoppedKey, false).toBool();
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
    connect(m_converter, &AudioConverter::started, this, [this](const QString &source, const QString &format) {
        m_downloads->setNote(m_conversions.value(source), tr("Converting to %1…").arg(format.toUpper()));
    });
    connect(m_converter, &AudioConverter::finished, this,
            [this](const QString &source, const QString &target, bool ok, const QString &error) {
                const QString downloadId = m_conversions.take(source);
                if (!ok) {
                    m_downloads->setNote(downloadId, tr("Conversion failed: %1").arg(error));
                    return;
                }
                // Recycle Bin rather than a hard delete, so a bad conversion is recoverable.
                if (!QFile::moveToTrash(source))
                    QFile::remove(source);
                importToLibrary(target, downloadId,
                                tr("Converted to %1 and added to the library").arg(QFileInfo(target).suffix().toUpper()));
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
            return tr("Enter your Soulseek username and password.");
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
        return tr("Running, but not logged in to Soulseek: check the username and password");
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
    if (format == m_downloadFormat || (format != kOriginalFormat && !AudioConverter::formats().contains(format)))
        return;
    m_downloadFormat = format;
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
        convertFinished(downloads);
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
            continue;
        }
        // Kept as downloaded: "original" chosen, or already in the chosen format (no re-encode).
        if (m_downloadFormat == kOriginalFormat
            || found->suffix().compare(m_downloadFormat, Qt::CaseInsensitive) == 0) {
            importToLibrary(found->absoluteFilePath(), download->id, tr("Added to the library"));
            continue;
        }
        m_conversions.insert(found->absoluteFilePath(), download->id);
        m_downloads->setNote(download->id, tr("Waiting to convert…"));
        m_converter->setFormat(m_downloadFormat);
        m_converter->convert(found->absoluteFilePath());
    }
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

    QString error;
    const QString moved = match.found
                              ? LibraryFiles::moveIntoFolder(source, match.folder, &error)
                              : LibraryFiles::moveIntoLibrary(source, m_downloadsDirectory, m_settings.libraryFolder(), &error);
    if (moved.isEmpty()) {
        m_downloads->setNote(downloadId, tr("Not added to the library: %1").arg(error));
        return;
    }
    m_downloads->setNote(downloadId, doneNote);
    emit trackImported(moved);
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

void SlskdController::setErrorText(const QString &text)
{
    if (text == m_errorText)
        return;
    m_errorText = text;
    emit errorTextChanged();
}
