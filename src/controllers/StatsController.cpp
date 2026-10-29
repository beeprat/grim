#include "controllers/StatsController.h"

#include "core/ArtistImageFetcher.h"
#include "providers/CoverImageProvider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJSEngine>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace {

constexpr int kTopCount = 10;

QVariantMap highlight(const StatsListModel::Entry &entry, const QString &image)
{
    return {{u"title"_s, entry.title},
            {u"subtitle"_s, entry.subtitle},
            {u"listenedMs"_s, entry.listenedMs},
            {u"playCount"_s, entry.playCount},
            {u"url"_s, entry.url},
            {u"image"_s, image}};
}

} // namespace

StatsController::StatsController(StatsDatabase &database, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_topTracks(new StatsListModel(this))
    , m_topArtists(new StatsListModel(this))
    , m_topAlbums(new StatsListModel(this))
    , m_artistImages(new ArtistImageFetcher(
          QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + u"/artist-photos"_s, this))
{
    Q_ASSERT(!s_instance);
    s_instance = this;
    // Square Deezer photos from before the YouTube Music banners.
    QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + u"/artists"_s).removeRecursively();
}

StatsController::~StatsController()
{
    s_instance = nullptr;
}

StatsController *StatsController::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "StatsController::create", "construct StatsController before loading QML");
    // Owned by main.cpp; stop the QML engine from deleting it.
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QVariantMap StatsController::artistStats(const QString &artist) const
{
    const ArtistStat stat = m_database.isOpen() ? m_database.artistStat(artist) : ArtistStat{};
    return {{u"playCount"_s, stat.playCount}, {u"listenedMs"_s, stat.listenedMs}};
}

QUrl StatsController::artistImage(const QString &artist)
{
    if (artist.isEmpty())
        return {};
    bool known = false;
    const QUrl image = m_artistImages->cached(artist, &known);
    if (!known)
        m_artistImages->fetch(artist, [this, artist](const QUrl &file) { emit artistImageReady(artist, file); });
    return image;
}

qint64 StatsController::totalPlays() const
{
    return m_totals.playCount;
}

qint64 StatsController::totalListenedMs() const
{
    return m_totals.listenedMs;
}

StatsListModel *StatsController::topTracks() const
{
    return m_topTracks;
}

StatsListModel *StatsController::topArtists() const
{
    return m_topArtists;
}

StatsListModel *StatsController::topAlbums() const
{
    return m_topAlbums;
}

QVariantMap StatsController::topTrack() const
{
    return m_topTrack;
}

QVariantMap StatsController::topArtist() const
{
    return m_topArtist;
}

QVariantMap StatsController::topAlbum() const
{
    return m_topAlbum;
}

QString StatsController::exportTo(const QUrl &fileUrl, const QString &libraryFolder)
{
    const QByteArray json = m_database.exportJson(libraryFolder);
    QSaveFile file(fileUrl.toLocalFile());
    if (json.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit())
        return tr("Could not export to %1").arg(QDir::toNativeSeparators(fileUrl.toLocalFile()));
    return {};
}

QString StatsController::importFrom(const QUrl &fileUrl, const QString &libraryFolder)
{
    QFile file(fileUrl.toLocalFile());
    if (!file.open(QIODevice::ReadOnly))
        return tr("Could not open %1").arg(QDir::toNativeSeparators(fileUrl.toLocalFile()));
    const int added = m_database.importJson(file.readAll(), libraryFolder);
    if (added < 0)
        return tr("%1 is not a grim. statistics export").arg(QFileInfo(file).fileName());
    refresh();
    return added == 1 ? tr("Imported 1 play") : tr("Imported %1 plays").arg(added);
}

void StatsController::refresh()
{
    // A failed open is already logged once; don't repeat a warning per query.
    if (!m_database.isOpen())
        return;

    m_totals = m_database.totals();
    emit totalsChanged();

    QList<StatsListModel::Entry> tracks;
    for (const TrackStat &stat : m_database.topTracks(kTopCount)) {
        const QString title = stat.track.title.isEmpty() ? QFileInfo(stat.track.path).completeBaseName()
                                                         : stat.track.title;
        tracks.append({title, stat.track.artist, stat.playCount, stat.listenedMs, QUrl::fromLocalFile(stat.track.path)});
    }

    QList<StatsListModel::Entry> artists;
    for (const ArtistStat &stat : m_database.topArtists(kTopCount))
        artists.append({stat.artist, QString(), stat.playCount, stat.listenedMs, QUrl()});

    QList<StatsListModel::Entry> albums;
    for (const AlbumStat &stat : m_database.topAlbums(kTopCount))
        albums.append({stat.album, stat.artist, stat.playCount, stat.listenedMs, QUrl::fromLocalFile(stat.samplePath)});

    // Highlights: covers come from the cover provider; the artist photo from the cache or Deezer,
    // looked up once per artist (refresh runs every few seconds).
    m_topTrack = tracks.isEmpty() ? QVariantMap()
                                  : highlight(tracks.first(), CoverImageProvider::urlFor(tracks.first().url.toLocalFile()));
    m_topAlbum = albums.isEmpty() ? QVariantMap()
                                  : highlight(albums.first(), CoverImageProvider::urlFor(albums.first().url.toLocalFile()));
    const QString artist = artists.isEmpty() ? QString() : artists.first().title;
    if (artist != m_imageArtist) {
        m_imageArtist = artist;
        bool known = false;
        m_artistImage = artist.isEmpty() ? QUrl() : m_artistImages->cached(artist, &known);
        if (!artist.isEmpty() && !known) {
            m_artistImages->fetch(artist, [this, artist](const QUrl &file) {
                if (artist != m_imageArtist)
                    return; // the top artist changed meanwhile
                m_artistImage = file;
                m_topArtist[u"image"_s] = file.toString();
                emit highlightsChanged();
            });
        }
    }
    m_topArtist = artists.isEmpty() ? QVariantMap() : highlight(artists.first(), m_artistImage.toString());
    m_topTracks->setEntries(std::move(tracks));
    m_topArtists->setEntries(std::move(artists));
    m_topAlbums->setEntries(std::move(albums));
    emit highlightsChanged();
}
