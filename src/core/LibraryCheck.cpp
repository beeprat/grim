#include "core/LibraryCheck.h"

#include <QFileInfo>
#include <QHash>

namespace LibraryCheck {

QList<Gap> missingTags(const QList<Track> &tracks, const std::function<bool(const QString &path)> &hasCover)
{
    QList<Gap> gaps;
    QHash<QString, qsizetype> byAlbum; // album key -> index in gaps
    for (const Track &track : tracks) {
        const QString key = track.album.toLower() + u'\n' + track.artist.toLower();
        auto it = byAlbum.constFind(key);
        if (it == byAlbum.cend()) {
            Gap gap;
            gap.album = track.album;
            gap.artist = track.artist;
            gap.cover = !hasCover(track.path);
            it = byAlbum.insert(key, gaps.size());
            gaps.append(gap);
        }
        Gap &gap = gaps[*it];
        gap.paths.append(track.path);
        gap.year = gap.year || track.year <= 0;
        gap.genre = gap.genre || track.genre.isEmpty();
        gap.trackNumber = gap.trackNumber || track.trackNumber <= 0;
    }
    gaps.removeIf([](const Gap &gap) { return !gap.cover && !gap.year && !gap.genre && !gap.trackNumber; });
    return gaps;
}

QList<QStringList> duplicates(const QList<Track> &tracks, const std::function<qint64(const QString &path)> &sizeOf)
{
    QHash<QString, QStringList> byKey;
    QStringList order; // keys in library order, so groups come out stable
    for (const Track &track : tracks) {
        const QString key = QFileInfo(track.path).fileName().toLower() + u'\n' + QString::number(sizeOf(track.path));
        QStringList &paths = byKey[key];
        if (paths.isEmpty())
            order.append(key);
        paths.append(track.path);
    }
    QList<QStringList> groups;
    for (const QString &key : order)
        if (byKey.value(key).size() > 1)
            groups.append(byKey.value(key));
    return groups;
}

} // namespace LibraryCheck
