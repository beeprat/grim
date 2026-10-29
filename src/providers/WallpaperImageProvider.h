#pragma once

#include <QQuickImageProvider>

// image://wallpaper/<blur in 1/1000>/<file URL, percent-encoded>: the background picture already
// blurred and darkened (core/WallpaperBlur).
class WallpaperImageProvider : public QQuickImageProvider
{
public:
    WallpaperImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
