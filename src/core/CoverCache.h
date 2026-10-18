#pragma once

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QSize>
#include <QString>

// Album art scaled to display size, cached in memory and as JPEG thumbnails on
// disk, so scrolling the library does not re-read tags and decode full-size
// covers. Entries are keyed by file path, modification time and size, so a
// retagged file gets a fresh thumbnail. Thread-safe (called from Qt Quick's
// image loader threads).
class CoverCache
{
public:
    // diskDirectory is created if missing; empty disables the disk cache.
    explicit CoverCache(const QString &diskDirectory, qint64 memoryBytes = 256LL * 1024 * 1024);

    // The cover of audioPath scaled to cover `size` (aspect kept, may exceed it in one
    // dimension). An invalid size returns the full image. Null if the file has no cover.
    QImage load(const QString &audioPath, const QSize &size);

private:
    QImage decode(const QString &audioPath, const QSize &size) const;

    QString m_directory;
    QMutex m_mutex;
    QCache<QString, QImage> m_memory; // cost = bytes; null images (no cover) cost 1
};
