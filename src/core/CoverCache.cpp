#include "core/CoverCache.h"

#include "core/TagReader.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>

using namespace Qt::StringLiterals;

CoverCache::CoverCache(const QString &diskDirectory, qint64 memoryBytes)
    : m_directory(diskDirectory)
{
    m_memory.setMaxCost(memoryBytes);
    if (!m_directory.isEmpty())
        QDir().mkpath(m_directory);
}

QImage CoverCache::decode(const QString &audioPath, const QSize &size) const
{
    QByteArray bytes = TagReader::readCover(audioPath);
    if (bytes.isEmpty())
        return {};
    QBuffer buffer(&bytes);
    QImageReader reader(&buffer);
    const QSize original = reader.size();
    // Let the decoder scale while decoding: for JPEG this skips most of the work on
    // large covers, which was the main cost while scrolling.
    if (size.isValid() && original.isValid() && (original.width() > size.width() || original.height() > size.height()))
        reader.setScaledSize(original.scaled(size, Qt::KeepAspectRatioByExpanding));
    return reader.read();
}

QString CoverCache::keyFor(const QString &audioPath, const QSize &size)
{
    const QFileInfo info(audioPath);
    return u"%1|%2|%3x%4"_s.arg(info.absoluteFilePath().toLower())
        .arg(info.lastModified().toMSecsSinceEpoch())
        .arg(size.width())
        .arg(size.height());
}

// Disk: "<hash>.jpg" holds the thumbnail; an empty "<hash>.none" remembers "no cover".
QString CoverCache::diskFile(const QString &key, const QString &extension) const
{
    if (m_directory.isEmpty())
        return {};
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
    return QDir(m_directory).filePath(hash + extension);
}

QString CoverCache::thumbnailFile(const QString &audioPath, const QSize &size)
{
    if (!size.isValid() || load(audioPath, size).isNull())
        return {};
    const QString thumbnail = diskFile(keyFor(audioPath, size), u".jpg"_s);
    return QFileInfo::exists(thumbnail) ? thumbnail : QString();
}

QImage CoverCache::load(const QString &audioPath, const QSize &size)
{
    const QString key = keyFor(audioPath, size);
    {
        const QMutexLocker lock(&m_mutex);
        if (const QImage *cached = m_memory.object(key))
            return *cached;
    }

    const QString thumbnail = diskFile(key, u".jpg"_s);
    const QString noCover = diskFile(key, u".none"_s);

    QImage image;
    if (!thumbnail.isEmpty() && QFileInfo::exists(thumbnail)) {
        image.load(thumbnail);
    } else if (noCover.isEmpty() || !QFileInfo::exists(noCover)) {
        image = decode(audioPath, size);
        if (!m_directory.isEmpty()) {
            if (image.isNull()) {
                QFile marker(noCover);
                if (!marker.open(QIODevice::WriteOnly))
                    qWarning().noquote() << "CoverCache: cannot write" << noCover;
            }
            else if (size.isValid()) // only thumbnails are worth storing; full images are the file's own
                image.save(thumbnail, "JPG", 90);
        }
    }

    const QMutexLocker lock(&m_mutex);
    m_memory.insert(key, new QImage(image), std::max<qint64>(1, image.sizeInBytes()));
    return image;
}
