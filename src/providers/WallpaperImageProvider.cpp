#include "WallpaperImageProvider.h"

#include "core/WallpaperBlur.h"

#include <QImageReader>
#include <QUrl>

WallpaperImageProvider::WallpaperImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage WallpaperImageProvider::requestImage(const QString &id, QSize *size, const QSize &)
{
    const qsizetype slash = id.indexOf(u'/');
    const qreal blur = id.left(slash).toInt() / 1000.0;
    const QUrl file(QUrl::fromPercentEncoding(id.mid(slash + 1).toUtf8()));
    QImageReader reader(file.toLocalFile());
    reader.setAutoTransform(true);
    const QImage image = WallpaperBlur::apply(reader.read(), blur);
    if (size)
        *size = image.size();
    return image;
}
