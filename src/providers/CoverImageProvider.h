#pragma once

#include "core/CoverCache.h"

#include <QQuickImageProvider>

// Serves album art as image://cover/<percent-encoded file path>: the embedded
// picture, or else a cover/folder image next to the file. Runs on Qt Quick's
// image loader threads; CoverCache keeps scaled covers on disk (and the most recent
// 32 MB in memory), so scrolling the library does not re-read tags or decode full-size art.
class CoverImageProvider : public QQuickImageProvider
{
public:
    explicit CoverImageProvider(const QString &cacheDirectory);

    // The image:// URL for a file's cover, for QML Image.source.
    static QString urlFor(const QString &filePath);
    // Changes every urlFor() result, so QML's image cache drops covers after a tag edit.
    static void bumpRevision();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    CoverCache m_cache;
};
