#include "controllers/SlskdController.h"

#include "controllers/SettingsController.h"
#include "core/LibraryFiles.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QSettings>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace {

constexpr int kSearchPollMs = 1000;
constexpr int kDownloadsPollMs = 1500;
// Status is checked only while the app starts (slskd may still be launching), then on demand:
// no background polling. kStartupChecks tries, kStatusPollMs apart.
constexpr int kStatusPollMs = 2000;
constexpr int kStartupChecks = 5;

const auto kServerUrlKey = "slskd/serverUrl"_L1;
const auto kDownloadFormatKey = "slskd/downloadFormat"_L1;
const auto kOriginalFormat = "original"_L1;
const auto kApiKeyKey = "slskd/apiKey"_L1;

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
{
    Q_ASSERT(!s_instance);
    s_instance = this;

    const QSettings stored;
    m_serverUrl = stored.value(kServerUrlKey, u"http://localhost:5030"_s).toString();
    // shortcut: API key in plain QSettings (HKCU registry); move to Windows Credential Manager if shared PCs matter.
    m_apiKey = stored.value(kApiKeyKey).toString();
    m_downloadFormat = stored.value(kDownloadFormatKey, u"m4a"_s).toString();
    if (m_downloadFormat != kOriginalFormat && !AudioConverter::formats().contains(m_downloadFormat))
        m_downloadFormat = u"m4a"_s;
    m_client->setServer(QUrl(m_serverUrl), m_apiKey);

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
        if (++m_startupChecks >= kStartupChecks || m_status == Online)
            m_statusTimer->stop();
        else
            checkStatus();
    });
    m_statusTimer->start();
    checkStatus();

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

QString SlskdController::serverUrl() const
{
    return m_serverUrl;
}

QString SlskdController::apiKey() const
{
    return m_apiKey;
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

void SlskdController::saveSettings(const QString &serverUrl, const QString &apiKey)
{
    m_serverUrl = serverUrl.trimmed();
    m_apiKey = apiKey.trimmed();
    QSettings settings;
    settings.setValue(kServerUrlKey, m_serverUrl);
    settings.setValue(kApiKeyKey, m_apiKey);
    m_client->setServer(QUrl(m_serverUrl), m_apiKey);
    setErrorText({});
    emit settingsChanged();
    recheckStatus();
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
    QString error;
    const QString moved = LibraryFiles::moveIntoLibrary(path, m_downloadsDirectory, m_settings.libraryFolder(), &error);
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

void SlskdController::setErrorText(const QString &text)
{
    if (text == m_errorText)
        return;
    m_errorText = text;
    emit errorTextChanged();
}
