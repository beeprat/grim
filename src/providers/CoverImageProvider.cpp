#include "providers/CoverImageProvider.h"

#include <QUrl>

CoverImageProvider::CoverImageProvider(const QString &cacheDirectory)
    : QQuickImageProvider(QQuickImageProvider::Image, QQuickImageProvider::ForceAsynchronousImageLoading)
    // Small: Qt Quick keeps its own copy of every cover on screen, so this only has to bridge
    // scrolling back and forth. Older covers come back from the disk thumbnails, which decode fast.
    , m_cache(cacheDirectory, 32LL * 1024 * 1024)
{
}

QString CoverImageProvider::urlFor(const QString &filePath)
{
    return QStringLiteral("image://cover/") + QString::fromUtf8(QUrl::toPercentEncoding(filePath));
}

QImage CoverImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // An empty image makes the QML Image show its placeholder.
    const QImage image = m_cache.load(QUrl::fromPercentEncoding(id.toUtf8()), requestedSize);
    if (size)
        *size = image.size();
    return image;
}
