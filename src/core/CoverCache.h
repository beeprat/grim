#pragma once

#include <QCache>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QSize>
#include <QString>

// Album art scaled to display size, cached in memory and as JPEG thumbnails on
// disk, so scrolling the library does not re-read tags and decode full-size
// covers. Thumbnails are stored once per cover picture and size (the tracks of an
// album share one); each track (path, modification time, size) points to its
// picture through a small file in "refs/", so a retagged file gets a fresh
// thumbnail. Thread-safe (called from Qt Quick's image loader threads).
class CoverCache
{
public:
    // diskDirectory is created if missing; empty disables the disk cache.
    explicit CoverCache(const QString &diskDirectory, qint64 memoryBytes = 256LL * 1024 * 1024);

    // The cover of audioPath scaled to cover `size` (aspect kept, may exceed it in one
    // dimension). An invalid size returns the full image. Null if the file has no cover.
    QImage load(const QString &audioPath, const QSize &size);
    // The JPEG thumbnail of load(audioPath, size) on disk (written if needed), for programs that want a
    // cover as a file (MPRIS art). Empty if there is no cover, no disk cache or `size` is invalid.
    QString thumbnailFile(const QString &audioPath, const QSize &size);

private:
    static QImage decode(QByteArray bytes, const QSize &size);
    static QString keyFor(const QString &audioPath, const QSize &size);
    static QString hashOf(const QByteArray &data);
    QString diskFile(const QString &key, const QString &extension) const;
    // Picture key (hash of the cover bytes and size) of a track key; empty if not known yet.
    QString pictureKey(const QString &trackKey);

    QString m_directory;
    QMutex m_mutex;
    QCache<QString, QImage> m_memory;  // by picture key; cost = bytes
    QHash<QString, QString> m_pictures; // track key -> picture key, mirrors refs/
};
