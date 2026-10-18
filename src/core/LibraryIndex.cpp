#include "core/LibraryIndex.h"

#include "core/LibraryFiles.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTimeZone>

namespace LibraryIndex {

namespace {

constexpr quint32 kMagic = 0x47524958; // "GRIX"
// Bump when Entry or Track gains or changes a field: older index files are then ignored.
constexpr quint32 kVersion = 1;

QDataStream &operator<<(QDataStream &out, const Entry &entry)
{
    const Track &t = entry.track;
    return out << t.path << t.title << t.artist << t.album << t.durationMs << qint32(t.year) << t.genre
               << qint32(t.trackNumber) << qint32(t.discNumber) << entry.size << entry.modifiedMs;
}

QDataStream &operator>>(QDataStream &in, Entry &entry)
{
    Track &t = entry.track;
    qint32 year = 0;
    qint32 trackNumber = 0;
    qint32 discNumber = 0;
    in >> t.path >> t.title >> t.artist >> t.album >> t.durationMs >> year >> t.genre >> trackNumber
        >> discNumber >> entry.size >> entry.modifiedMs;
    t.year = year;
    t.trackNumber = trackNumber;
    t.discNumber = discNumber;
    return in;
}

} // namespace

Known load(const QString &indexFile, const QString &folder)
{
    QFile file(indexFile);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_8);
    quint32 magic = 0;
    quint32 version = 0;
    QString indexedFolder;
    quint32 count = 0;
    in >> magic >> version >> indexedFolder >> count;
    if (in.status() != QDataStream::Ok || magic != kMagic || version != kVersion
        || QDir::cleanPath(indexedFolder).compare(QDir::cleanPath(folder), Qt::CaseInsensitive) != 0)
        return {};

    Known known;
    known.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        Entry entry;
        in >> entry;
        if (in.status() != QDataStream::Ok)
            return {}; // truncated or damaged: start over rather than half a library
        known.insert(entry.track.path.toLower(), entry);
    }
    return known;
}

bool save(const QString &indexFile, const QString &folder, const QList<Entry> &entries)
{
    // QSaveFile: a crash mid-write leaves the previous index, never a broken one.
    QSaveFile file(indexFile);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_8);
    out << kMagic << kVersion << folder << quint32(entries.size());
    for (const Entry &entry : entries)
        out << entry;
    return out.status() == QDataStream::Ok && file.commit();
}

ScanResult scan(const QString &folder, const Known &known, Track (*read)(const QString &path))
{
    QStringList patterns;
    for (const QString &extension : LibraryFiles::playableExtensions())
        patterns << QStringLiteral("*.") + extension;

    ScanResult result;
    qsizetype reused = 0;
    QDirIterator it(folder, patterns, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        // Skip half-written conversion output.
        if (info.completeBaseName().endsWith(QStringLiteral(".converting"), Qt::CaseInsensitive))
            continue;
        const QString path = info.filePath();
        Entry entry;
        entry.size = info.size();
        entry.modifiedMs = info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
        const auto cached = known.constFind(path.toLower());
        if (cached != known.cend() && cached->size == entry.size && cached->modifiedMs == entry.modifiedMs) {
            entry.track = cached->track;
            ++reused;
        } else {
            entry.track = read(path);
            result.changed = true;
        }
        result.entries.append(entry);
    }
    // Every file reused, and nothing left over in `known`: the library is as indexed.
    if (reused != known.size())
        result.changed = true;
    return result;
}

QList<Track> tracksOf(const QList<Entry> &entries)
{
    QList<Track> tracks;
    tracks.reserve(entries.size());
    for (const Entry &entry : entries)
        tracks.append(entry.track);
    return tracks;
}

Known keyed(const QList<Entry> &entries)
{
    Known known;
    known.reserve(entries.size());
    for (const Entry &entry : entries)
        known.insert(entry.track.path.toLower(), entry);
    return known;
}

} // namespace LibraryIndex
