#include "controllers/LibraryController.h"

#include "controllers/SettingsController.h"
#include "core/LibraryDatabase.h"
#include "core/LibraryFiles.h"
#include "core/TagReader.h"

#include <QDir>
#ifndef Q_OS_WIN
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDesktopServices>
#endif
#include <QFile>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QJSEngine>
#include <QTimer>
#include <QtConcurrentRun>


LibraryController::LibraryController(SettingsController &settings, LibraryDatabase &database, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_database(database)
    , m_tracks(new LibraryModel(this))
    , m_pickerTracks(new LibraryModel(this))
    , m_albums(new AlbumsModel(*m_tracks, [&database](const QString &path) { return database.rating(path); }, this))
{
    Q_ASSERT(!s_instance);
    s_instance = this;
    m_tracks->setRatingSource([&database](const QString &path) { return database.rating(path); });

    m_filterDelay = new QTimer(this);
    m_filterDelay->setSingleShot(true);
    m_filterDelay->setInterval(150);
    connect(m_filterDelay, &QTimer::timeout, this, [this] {
        m_albums->setFilter(m_filter);
        m_tracks->setFilter(m_filter);
    });

    connect(&m_scan, &QFutureWatcher<LibraryIndex::ScanResult>::finished, this, [this] {
        if (m_rescanPending) {
            m_rescanPending = false;
            rescan();
            return;
        }
        const LibraryIndex::ScanResult result = m_scan.result();
        // Unchanged since the index was shown: no model reset, so views keep their scroll position.
        if (result.changed) {
            const QList<Track> tracks = LibraryIndex::tracksOf(result.entries);
            m_tracks->setTracks(tracks);
            m_pickerTracks->setTracks(tracks);
        }
        emit scanningChanged();
    });
    connect(&m_settings, &SettingsController::libraryFolderChanged, this, &LibraryController::rescan);

    // Show the library as last indexed right away, then check the folder for changes.
    m_indexFile = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/library-index.bin");
    const LibraryIndex::Known known = LibraryIndex::load(m_indexFile, m_settings.libraryFolder());
    if (!known.isEmpty()) {
        QList<LibraryIndex::Entry> entries = known.values();
        const QList<Track> tracks = LibraryIndex::tracksOf(entries);
        m_tracks->setTracks(tracks); // sorts them
        m_pickerTracks->setTracks(tracks);
    }
    startScan(false);
}

LibraryController::~LibraryController()
{
    // The scan only reads files; let it finish rather than destroy state under it.
    m_scan.waitForFinished();
    s_instance = nullptr;
}

LibraryController *LibraryController::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "LibraryController::create", "construct LibraryController before loading QML");
    // Owned by main.cpp; stop the QML engine from deleting it.
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

LibraryModel *LibraryController::tracks() const
{
    return m_tracks;
}

AlbumsModel *LibraryController::albums() const
{
    return m_albums;
}

QString LibraryController::filter() const
{
    return m_filter;
}

void LibraryController::setFilter(const QString &text)
{
    if (text == m_filter)
        return;
    m_filter = text;
    // Typing restarts the delay, so the models filter once per pause, not once per key.
    // Clearing applies at once: openAlbumOf() clears the search and looks the album up right after.
    if (text.isEmpty()) {
        m_filterDelay->stop();
        m_albums->setFilter(text);
        m_tracks->setFilter(text);
    } else {
        m_filterDelay->start();
    }
    emit filterChanged();
}

QString LibraryController::genreFilter() const
{
    return m_genreFilter;
}

void LibraryController::setGenreFilter(const QString &key)
{
    if (key == m_genreFilter)
        return;
    m_genreFilter = key;
    m_albums->setGenreFilter(key);
    emit genreFilterChanged();
}

QString LibraryController::artistFilter() const
{
    return m_artistFilter;
}

void LibraryController::setArtistFilter(const QString &artist)
{
    if (artist == m_artistFilter)
        return;
    m_artistFilter = artist;
    m_albums->setArtistFilter(artist);
    emit artistFilterChanged();
}

LibraryModel *LibraryController::pickerTracks() const
{
    return m_pickerTracks;
}

int LibraryController::metaRevision() const
{
    return m_metaRevision;
}

int LibraryController::rating(const QUrl &fileUrl) const
{
    return m_database.rating(fileUrl.toLocalFile());
}

void LibraryController::setRating(const QUrl &fileUrl, int stars)
{
    if (m_database.setRating(fileUrl.toLocalFile(), stars)) {
        ++m_metaRevision;
        emit metaChanged();
        m_albums->ratingsChanged();
        m_tracks->ratingsChanged();
    }
}

bool LibraryController::isFavorite(const QUrl &fileUrl) const
{
    return m_database.isFavorite(fileUrl.toLocalFile());
}

void LibraryController::toggleFavorite(const QUrl &fileUrl)
{
    const QString path = fileUrl.toLocalFile();
    if (m_database.setFavorite(path, !m_database.isFavorite(path))) {
        ++m_metaRevision;
        emit metaChanged();
    }
}

void LibraryController::showInFolder(const QUrl &fileUrl) const
{
    const QString path = QDir::toNativeSeparators(fileUrl.toLocalFile());
    if (path.isEmpty())
        return;
#ifdef Q_OS_WIN
    // explorer.exe wants `/select,"<path>"` as one raw argument; QProcess quoting would break it.
    QProcess explorer;
    explorer.setProgram(QStringLiteral("explorer.exe"));
    explorer.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(path));
    explorer.startDetached();
#else
    // Ask the file manager to open the folder with the file selected; just the folder if none answers.
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("/org/freedesktop/FileManager1"),
        QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
    call << QStringList{fileUrl.toString()} << QString();
    if (QDBusConnection::sessionBus().call(call, QDBus::Block, 3000).type() != QDBusMessage::ReplyMessage)
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

const Track *LibraryController::findTrack(const QString &path) const
{
    return m_pickerTracks->findTrack(path);
}

bool LibraryController::scanning() const
{
    return m_scan.isRunning();
}

void LibraryController::rescan()
{
    startScan(true);
}

void LibraryController::startScan(bool full)
{
    // QtConcurrent::run cannot be cancelled; restart once the running scan ends.
    if (m_scan.isRunning()) {
        m_rescanPending = true;
        return;
    }
    const QString folder = m_settings.libraryFolder();
    const QString indexFile = m_indexFile;
    m_scan.setFuture(QtConcurrent::run([folder, indexFile, full] {
        const LibraryIndex::Known known = full ? LibraryIndex::Known() : LibraryIndex::load(indexFile, folder);
        LibraryIndex::ScanResult result = LibraryIndex::scan(folder, known, &TagReader::read);
        if (result.changed || full)
            LibraryIndex::save(indexFile, folder, result.entries);
        // A full scan always replaces the shown tracks (they may have been edited in place).
        result.changed = result.changed || full;
        return result;
    }));
    emit scanningChanged();
}

QString LibraryController::deleteAlbum(const QList<QUrl> &fileUrls)
{
    QStringList paths;
    for (const QUrl &url : fileUrls)
        paths.append(url.toLocalFile());
    if (paths.isEmpty())
        return {};
    emit aboutToDelete(paths);

    const QStringList failed = LibraryFiles::deleteAlbum(paths, m_settings.libraryFolder(), [](const QString &path) {
        // Recycle Bin: a wrong click can be undone from there.
        return QFile::moveToTrash(path);
    });
    QStringList gone;
    for (const QString &path : paths) {
        if (!QFileInfo::exists(path))
            gone.append(path);
    }
    m_tracks->removeTracks(gone);
    m_pickerTracks->removeTracks(gone);
    if (failed.isEmpty())
        return {};
    return tr("Could not delete %1").arg(QDir::toNativeSeparators(failed.first()))
           + (failed.size() > 1 ? tr(" and %1 more").arg(failed.size() - 1) : QString());
}

void LibraryController::addFile(const QString &path)
{
    if (!LibraryFiles::isPlayable(path))
        return;
    const Track track = TagReader::read(path);
    m_tracks->addTrack(track);
    m_pickerTracks->addTrack(track);
    // A running scan may have passed this folder already; its result would drop the file.
    if (m_scan.isRunning())
        m_rescanPending = true;
}
