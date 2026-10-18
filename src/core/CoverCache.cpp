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

QImage CoverCache::load(const QString &audioPath, const QSize &size)
{
    const QFileInfo info(audioPath);
    const QString key = u"%1|%2|%3x%4"_s.arg(info.absoluteFilePath().toLower())
                            .arg(info.lastModified().toMSecsSinceEpoch())
                            .arg(size.width())
                            .arg(size.height());
    {
        const QMutexLocker lock(&m_mutex);
        if (const QImage *cached = m_memory.object(key))
            return *cached;
    }

    // Disk: "<hash>.jpg" holds the thumbnail; an empty "<hash>.none" remembers "no cover".
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
    const QString thumbnail = m_directory.isEmpty() ? QString() : QDir(m_directory).filePath(hash + u".jpg"_s);
    const QString noCover = m_directory.isEmpty() ? QString() : QDir(m_directory).filePath(hash + u".none"_s);

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
