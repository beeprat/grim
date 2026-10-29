#include "providers/CoverImageProvider.h"

#include <QUrl>

#include <atomic>

CoverImageProvider::CoverImageProvider(const QString &cacheDirectory)
    : QQuickImageProvider(QQuickImageProvider::Image, QQuickImageProvider::ForceAsynchronousImageLoading)
    // Small: Qt Quick keeps its own copy of every cover on screen, so this only has to bridge
    // scrolling back and forth. Older covers come back from the disk thumbnails, which decode fast.
    , m_cache(cacheDirectory, 32LL * 1024 * 1024)
{
}

namespace {

std::atomic_int s_revision{0}; // appended as "?r=<n>" once a cover was edited

} // namespace

QString CoverImageProvider::urlFor(const QString &filePath)
{
    const QString url = QStringLiteral("image://cover/") + QString::fromUtf8(QUrl::toPercentEncoding(filePath));
    const int revision = s_revision;
    return revision > 0 ? url + QStringLiteral("?r=") + QString::number(revision) : url;
}

void CoverImageProvider::bumpRevision()
{
    ++s_revision;
}

QImage CoverImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // An empty image makes the QML Image show its placeholder.
    // The path is percent-encoded, so a "?" can only start the revision.
    const QImage image = m_cache.load(QUrl::fromPercentEncoding(id.section(u'?', 0, 0).toUtf8()), requestedSize);
    if (size)
        *size = image.size();
    return image;
}
