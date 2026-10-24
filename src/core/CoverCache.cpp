#include "core/CoverCache.h"

#include "core/TagReader.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>

using namespace Qt::StringLiterals;

namespace {

const auto kRefsFolder = u"refs"_s;
// Thumbnails are small; 80 looks the same as 90 there and takes about a third less space.
constexpr int kJpegQuality = 80;

} // namespace

CoverCache::CoverCache(const QString &diskDirectory, qint64 memoryBytes)
    : m_directory(diskDirectory)
{
    m_memory.setMaxCost(memoryBytes);
    if (m_directory.isEmpty())
        return;
    QDir dir(m_directory);
    // Older builds kept one thumbnail per track; no "refs" folder means those are still here.
    if (dir.exists() && !dir.exists(kRefsFolder)) {
        for (const QString &old : dir.entryList({u"*.jpg"_s}, QDir::Files))
            dir.remove(old);
    }
    dir.mkpath(kRefsFolder);
}

QImage CoverCache::decode(QByteArray bytes, const QSize &size)
{
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

QString CoverCache::hashOf(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
}

// Disk: "<hash>.jpg" holds a thumbnail (hash of the picture key); an empty "<hash>.none" remembers that a
// track has no cover; "refs/<hash>" (hash of the track key) holds the track's picture key.
QString CoverCache::diskFile(const QString &key, const QString &extension) const
{
    if (m_directory.isEmpty())
        return {};
    return QDir(m_directory).filePath(hashOf(key.toUtf8()) + extension);
}

QString CoverCache::pictureKey(const QString &trackKey)
{
    {
        const QMutexLocker lock(&m_mutex);
        if (const auto it = m_pictures.constFind(trackKey); it != m_pictures.cend())
            return *it;
    }
    if (m_directory.isEmpty())
        return {};
    QFile ref(QDir(m_directory).filePath(kRefsFolder + u'/' + hashOf(trackKey.toUtf8())));
    if (!ref.open(QIODevice::ReadOnly))
        return {};
    const QString key = QString::fromUtf8(ref.readAll());
    const QMutexLocker lock(&m_mutex);
    m_pictures.insert(trackKey, key);
    return key;
}

QString CoverCache::thumbnailFile(const QString &audioPath, const QSize &size)
{
    if (!size.isValid() || load(audioPath, size).isNull())
        return {};
    const QString thumbnail = diskFile(pictureKey(keyFor(audioPath, size)), u".jpg"_s);
    return QFileInfo::exists(thumbnail) ? thumbnail : QString();
}

QImage CoverCache::load(const QString &audioPath, const QSize &size)
{
    const QString trackKey = keyFor(audioPath, size);
    QString picture = pictureKey(trackKey);
    if (!picture.isEmpty()) {
        {
            const QMutexLocker lock(&m_mutex);
            if (const QImage *cached = m_memory.object(picture))
                return *cached;
        }
        QImage image(diskFile(picture, u".jpg"_s));
        if (!image.isNull()) {
            const QMutexLocker lock(&m_mutex);
            m_memory.insert(picture, new QImage(image), std::max<qint64>(1, image.sizeInBytes()));
            return image;
        }
        // Thumbnail deleted (cache cleared): read the file again below.
    }

    const QString noCover = diskFile(trackKey, u".none"_s);
    if (!noCover.isEmpty() && QFileInfo::exists(noCover))
        return {};

    const QByteArray bytes = TagReader::readCover(audioPath);
    if (bytes.isEmpty()) {
        if (!noCover.isEmpty()) {
            QFile marker(noCover);
            if (!marker.open(QIODevice::WriteOnly))
                qWarning().noquote() << "CoverCache: cannot write" << noCover;
        }
        return {};
    }

    picture = u"%1|%2x%3"_s.arg(hashOf(bytes)).arg(size.width()).arg(size.height());
    {
        const QMutexLocker lock(&m_mutex);
        m_pictures.insert(trackKey, picture);
    }
    QImage image;
    {
        const QMutexLocker lock(&m_mutex);
        if (const QImage *cached = m_memory.object(picture))
            image = *cached;
    }
    const QString thumbnail = diskFile(picture, u".jpg"_s);
    if (image.isNull() && !thumbnail.isEmpty())
        image.load(thumbnail); // another track of the album made it already
    if (image.isNull()) {
        image = decode(bytes, size);
        if (image.isNull())
            return {};
        if (!thumbnail.isEmpty() && size.isValid()) // only thumbnails are worth storing; full images are the file's own
            image.save(thumbnail, "JPG", kJpegQuality);
    }
    if (!m_directory.isEmpty() && size.isValid()) {
        QFile ref(QDir(m_directory).filePath(kRefsFolder + u'/' + hashOf(trackKey.toUtf8())));
        if (ref.open(QIODevice::WriteOnly))
            ref.write(picture.toUtf8());
    }

    const QMutexLocker lock(&m_mutex);
    m_memory.insert(picture, new QImage(image), std::max<qint64>(1, image.sizeInBytes()));
    return image;
}
