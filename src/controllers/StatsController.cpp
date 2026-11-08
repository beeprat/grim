#include "controllers/StatsController.h"

#include "core/ArtistImageFetcher.h"
#include "providers/CoverImageProvider.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QJSEngine>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace {

constexpr int kTopCount = 10;
constexpr int kGenreSlices = 7; // the rest go into "Other"
constexpr int kMaxGraphDays = 365;

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

qint64 StatsController::allTimeListenedMs() const
{
    return m_allTimeListenedMs;
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

int StatsController::rangeDays() const
{
    return m_rangeDays;
}

void StatsController::setRangeDays(int days)
{
    if (days == m_rangeDays || days < 0)
        return;
    m_rangeDays = days;
    emit rangeDaysChanged();
    refresh();
}

QVariantList StatsController::genreShares() const
{
    return m_genreShares;
}

QVariantList StatsController::dailyListening() const
{
    return m_dailyListening;
}

void StatsController::setGenreLookup(std::function<QString(const QString &)> lookup)
{
    m_genreOf = std::move(lookup);
}

void StatsController::setLibraryData(std::function<QJsonObject(const QString &)> save,
                                     std::function<void(const QJsonObject &, const QString &)> load)
{
    m_saveLibrary = std::move(save);
    m_loadLibrary = std::move(load);
}

QVariantList StatsController::neverPlayed(const QVariantList &albums, int limit) const
{
    const QSet<QString> played = m_database.isOpen() ? m_database.playedAlbums() : QSet<QString>();
    QVariantList result;
    for (const QVariant &entry : albums) {
        if (result.size() >= limit)
            break;
        const QVariantMap album = entry.toMap();
        if (!played.contains(album.value(u"title"_s).toString() + u'\n' + album.value(u"artist"_s).toString()))
            result.append(album);
    }
    return result;
}

QString StatsController::exportTo(const QUrl &fileUrl, const QString &libraryFolder)
{
    QByteArray json = m_database.exportJson(libraryFolder);
    if (!json.isEmpty() && m_saveLibrary && !libraryFolder.isEmpty()) {
        QJsonObject root = QJsonDocument::fromJson(json).object();
        root.insert(u"library"_s, m_saveLibrary(libraryFolder));
        json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    }
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
    const QByteArray json = file.readAll();
    const int added = m_database.importJson(json, libraryFolder);
    if (added < 0)
        return tr("%1 is not a grim. statistics export").arg(QFileInfo(file).fileName());
    // Ratings, favorites and playlists the export carries (versions before 0.4.1 have none).
    const QJsonObject library = QJsonDocument::fromJson(json).object().value("library"_L1).toObject();
    if (m_loadLibrary && !library.isEmpty())
        m_loadLibrary(library, libraryFolder);
    refresh();
    return added == 1 ? tr("Imported 1 play") : tr("Imported %1 plays").arg(added);
}

QVariantList StatsController::homeAlbums(bool recent, int limit) const
{
    QVariantList result;
    // Asks for extra rows: albums deleted since they were played are skipped.
    for (const AlbumStat &stat : m_database.topAlbums(limit * 3, recent)) {
        if (result.size() >= limit)
            break;
        if (!QFileInfo::exists(stat.samplePath))
            continue;
        result.append(QVariantMap{{u"title"_s, stat.album},
                                  {u"artist"_s, stat.artist},
                                  {u"coverUrl"_s, CoverImageProvider::urlFor(stat.samplePath)},
                                  {u"url"_s, QUrl::fromLocalFile(stat.samplePath)}});
    }
    return result;
}

void StatsController::refresh()
{
    // A failed open is already logged once; don't repeat a warning per query.
    if (!m_database.isOpen())
        return;

    const QDate today = QDate::currentDate();
    // Range starts at local midnight, so "7 days" is today and the 6 before.
    const qint64 since = m_rangeDays > 0 ? QDateTime(today.addDays(1 - m_rangeDays), QTime(0, 0)).toSecsSinceEpoch() : 0;

    m_totals = m_database.totals(since);
    m_allTimeListenedMs = since > 0 ? m_database.totals().listenedMs : m_totals.listenedMs;
    emit totalsChanged();

    // Genre slices.
    QHash<QString, qint64> byGenre;
    for (const auto &[path, ms] : m_database.listenedByPath(since)) {
        const QString genre = m_genreOf ? m_genreOf(path).trimmed() : QString();
        byGenre[genre] += ms; // empty: no genre tag, named "Unknown" below
    }
    QList<QPair<QString, qint64>> genres(byGenre.keyValueBegin(), byGenre.keyValueEnd());
    std::sort(genres.begin(), genres.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    m_genreShares.clear();
    qint64 other = 0;
    for (qsizetype i = 0; i < genres.size(); ++i) {
        if (i < kGenreSlices)
            m_genreShares.append(QVariantMap{{u"genre"_s, genres[i].first.isEmpty() ? tr("Unknown") : genres[i].first},
                                             {u"key"_s, genres[i].first.toLower()}, // the library's genre filter key
                                             {u"ms"_s, genres[i].second}});
        else
            other += genres[i].second;
    }
    if (other > 0)
        m_genreShares.append(QVariantMap{{u"genre"_s, tr("Other")}, {u"key"_s, QString()}, {u"ms"_s, other}});

    // Daily bars, zero-filled. All time starts at the first day listened.
    // shortcut: today's UTC offset is used for every day, so days across a DST change shift by an hour; fine for a chart.
    const int offset = QDateTime::currentDateTime().offsetFromUtc();
    const auto days = m_database.listenedByDay(since, offset);
    const qint64 todayNumber = (QDateTime::currentSecsSinceEpoch() + offset) / 86400;
    qint64 first = m_rangeDays > 0 ? todayNumber - m_rangeDays + 1 : (days.isEmpty() ? todayNumber : days.first().first);
    first = std::max(first, todayNumber - kMaxGraphDays + 1);
    QHash<qint64, qint64> msByDay;
    for (const auto &[day, ms] : days)
        msByDay.insert(day, ms);
    m_dailyListening.clear();
    for (qint64 day = first; day <= todayNumber; ++day)
        m_dailyListening.append(QVariantMap{{u"date"_s, today.addDays(day - todayNumber)}, {u"ms"_s, msByDay.value(day)}});
    emit chartsChanged();

    QList<StatsListModel::Entry> tracks;
    for (const TrackStat &stat : m_database.topTracks(kTopCount, since)) {
        const QString title = stat.track.title.isEmpty() ? QFileInfo(stat.track.path).completeBaseName()
                                                         : stat.track.title;
        tracks.append({title, stat.track.artist, stat.playCount, stat.listenedMs, QUrl::fromLocalFile(stat.track.path)});
    }

    QList<StatsListModel::Entry> artists;
    for (const ArtistStat &stat : m_database.topArtists(kTopCount, since))
        artists.append({stat.artist, QString(), stat.playCount, stat.listenedMs, QUrl()});

    QList<StatsListModel::Entry> albums;
    for (const AlbumStat &stat : m_database.topAlbums(kTopCount, false, since))
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
